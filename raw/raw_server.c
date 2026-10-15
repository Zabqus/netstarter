/*
 * raw_server.c – serwer echo na gniazdach surowych (warstwa 3), protokół NS (IP proto 253).
 *
 * Uruchomienie (wymaga CAP_NET_RAW):
 *   sudo ./build/raw_server [-4]           (domyślnie IPv4)
 *
 * Sekwencja wywołań:
 *   socket(AF_INET, SOCK_RAW, 253) → recvfrom (pętla) → [weryfikacja nagłówka NS] →
 *   sendto (odpowiedź REP) → close
 *
 * Nie ma bind(), listen() ani portów: gniazdo surowe dostaje KAŻDY pakiet IP
 * z protokołem 253, który dotrze do tej maszyny (na dowolny jej adres).
 *
 * IPv4: recvfrom() zwraca pakiet RAZEM z nagłówkiem IP – nasze dane zaczynają się
 * od buf + ip->ihl * 4. Serwer wypisuje wybrane pola tego nagłówka.
 * Ctrl+C (SIGINT) kończy serwer i zamyka gniazdo.
 */
#include <errno.h>       /* errno, EINTR */
#include <signal.h>      /* sigaction, sig_atomic_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp */
#include <unistd.h>      /* close */
#include <arpa/inet.h>   /* inet_ntop, ntohs */
#include <netinet/in.h>  /* struct in_addr */
#include <netinet/ip.h>  /* struct iphdr – nagłówek IPv4 */
#include <sys/socket.h>  /* recvfrom, sendto */

#include "net_common.h"
#include "raw_proto.h"

#define RECV_BUF_SIZE 65536   /* maksymalny rozmiar pakietu IP */

/* Jedyna zmienna globalna: flaga ustawiana przez obsługę SIGINT. */
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4]\n"
                    "  -4   IPv4 (domyślnie)\n"
                    "Wymaga CAP_NET_RAW: sudo albo scripts/setcap.sh\n", prog);
    exit(EXIT_FAILURE);
}

/* Wypisuje wybrane pola nagłówka IPv4 odebranego pakietu. */
static void print_ipv4_header(const struct iphdr *ip)
{
    /*
     * Adresy w nagłówku to surowe 32-bitowe liczby (network byte order), a nie
     * struktury sockaddr – do zamiany na tekst używamy więc inet_ntop().
     */
    char src[INET_ADDRSTRLEN];
    char dst[INET_ADDRSTRLEN];
    struct in_addr a;

    a.s_addr = ip->saddr;
    inet_ntop(AF_INET, &a, src, sizeof src);
    a.s_addr = ip->daddr;
    inet_ntop(AF_INET, &a, dst, sizeof dst);

    printf("[RAW][srv]   IPv4: src=%s dst=%s ttl=%u proto=%u hlen=%u B tot_len=%u B\n",
           src, dst, (unsigned)ip->ttl, (unsigned)ip->protocol,
           (unsigned)ip->ihl * 4, (unsigned)ntohs(ip->tot_len));
}

int main(int argc, char *argv[])
{
    int family = AF_INET;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-4") == 0)
            family = AF_INET;
        else
            usage(argv[0]);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Ctrl+C bez SA_RESTART – recvfrom() zwróci -1 z EINTR i pętla się zakończy. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        die("sigaction");

    int fd = ns_open_socket(family);
    printf("[RAW][srv] czekam na pakiety IPv4 protokołu %d (Ctrl+C kończy)\n", NS_PROTO);

    uint8_t buf[RECV_BUF_SIZE];
    uint8_t reply[sizeof(struct ns_hdr) + NS_MAX_DATA];
    char peer_str[ADDR_STR_LEN];

    while (!g_stop) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof peer;

        /* recvfrom(): jeden pakiet IP; peer = adres nadawcy (bez portu – to warstwa 3). */
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&peer, &peer_len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recvfrom");
            continue;
        }
        addr_to_host((struct sockaddr *)&peer, peer_len, peer_str, sizeof peer_str);

        /*
         * IPv4: na początku bufora jest nagłówek IP. Jego długość jest zmienna
         * (opcje IP) – pole ihl podaje ją w słowach 32-bitowych, stąd * 4.
         */
        if ((size_t)n < sizeof(struct iphdr))
            continue;
        const struct iphdr *ip = (const struct iphdr *)buf;
        size_t ip_hlen = (size_t)ip->ihl * 4;
        if (ip_hlen < sizeof(struct iphdr) || ip_hlen > (size_t)n)
            continue;

        const uint8_t *pkt = buf + ip_hlen;        /* tu zaczyna się nagłówek NS */
        size_t pkt_len = (size_t)n - ip_hlen;

        const char *err = ns_validate(pkt, pkt_len);
        if (err != NULL) {
            printf("[RAW][srv] odrzucony pakiet od %s: %s\n", peer_str, err);
            continue;
        }

        const struct ns_hdr *h = (const struct ns_hdr *)pkt;

        /*
         * Gniazdo surowe dostaje kopię KAŻDEGO pakietu protokołu 253 – także naszych
         * własnych odpowiedzi REP, gdy klient jest na tej samej maszynie (127.0.0.1).
         * Obsługujemy tylko żądania.
         */
        if (h->type != NS_TYPE_REQ)
            continue;

        uint16_t id = ntohs(h->id);
        uint16_t seq = ntohs(h->seq);
        uint16_t data_len = ntohs(h->length);
        const uint8_t *data = pkt + sizeof *h;

        printf("[RAW][srv] REQ od %s id=%u seq=%u, %u B: \"%.*s\"\n",
               peer_str, (unsigned)id, (unsigned)seq, (unsigned)data_len,
               (int)data_len, (const char *)data);
        print_ipv4_header(ip);

        if (data_len > NS_MAX_DATA) {
            printf("[RAW][srv]   za dużo danych – nie odpowiadam\n");
            continue;
        }

        /*
         * Odpowiedź: te same id, seq i dane, typ REP. Budujemy tylko nagłówek NS –
         * nagłówek IP (adresy, TTL, protocol = 253, suma IP) dokłada jądro.
         * sendto() na adres nadawcy z recvfrom().
         */
        size_t reply_len = ns_build(reply, NS_TYPE_REP, id, seq, data, data_len);
        if (sendto(fd, reply, reply_len, 0, (struct sockaddr *)&peer, peer_len) < 0)
            perror("sendto");
        else
            printf("[RAW][srv]   odesłano REP (%zu B)\n", reply_len);
    }

    printf("\n[RAW][srv] zamykam serwer\n");
    close(fd);
    return 0;
}
