/*
 * raw_server.c – serwer echo na gniazdach surowych (warstwa 3), protokół NS (IP proto 253).
 *
 * Uruchomienie (wymaga CAP_NET_RAW):
 *   sudo ./build/raw_server [-4|-6]        albo bez sudo po scripts/setcap.sh
 *
 * Sekwencja wywołań:
 *   socket(AF_INET/AF_INET6, SOCK_RAW, 253) → recvfrom (pętla) →
 *   [sprawdzenie pakietu NS] → sendto (odpowiedź REP) → close
 *
 * Nie ma bind(), listen() ani portów – gniazdo dostaje każdy pakiet z protokołem 253.
 * IPv4: recvfrom() zwraca pakiet z nagłówkiem IP, IPv6: bez nagłówka (ns_locate()).
 */
#include <errno.h>       /* errno, EINTR */
#include <signal.h>      /* sigaction, sig_atomic_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset */
#include <unistd.h>      /* close */
#include <arpa/inet.h>   /* inet_ntop, ntohs */
#include <netinet/in.h>  /* struct in_addr */
#include <netinet/ip.h>  /* struct iphdr – nagłówek IPv4 */
#include <sys/socket.h>  /* recvfrom, sendto */

#include "net_common.h"
#include "raw_proto.h"

#define RECV_BUF_SIZE 65536   /* maks. rozmiar pakietu IP */

// Jedyna zmienna globalna – flaga ustawiana po Ctrl+C.
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4|-6]\n"
                    "  -4   IPv4 (domyślnie)\n"
                    "  -6   IPv6\n"
                    "Wymaga CAP_NET_RAW: sudo albo scripts/setcap.sh\n", prog);
    exit(EXIT_FAILURE);
}

// Wypisuje wybrane pola nagłówka IPv4.
static void print_ipv4_header(const struct iphdr *ip)
{
    char src[INET_ADDRSTRLEN];
    char dst[INET_ADDRSTRLEN];
    struct in_addr a;

    // adresy w nagłówku to surowe liczby 32-bit – zamieniamy na tekst
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
        int f = parse_family(argv[i]);
        if (f != -1)
            family = f;
        else
            usage(argv[0]);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    // Ctrl+C: bez SA_RESTART, żeby recvfrom() się przerwało
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        die("sigaction");

    // tworzymy gniazdo surowe protokołu 253
    int fd = ns_open_socket(family);

    printf("[RAW][srv] czekam na pakiety %s protokołu %d (Ctrl+C kończy)\n",
           family == AF_INET6 ? "IPv6" : "IPv4", NS_PROTO);

    uint8_t buf[RECV_BUF_SIZE];
    uint8_t reply[sizeof(struct ns_hdr) + NS_MAX_DATA];
    char peer_str[ADDR_STR_LEN];

    while (!g_stop) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof peer;

        // odbieramy jeden pakiet IP i adres nadawcy (bez portu)
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&peer, &peer_len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recvfrom");
            continue;
        }
        addr_to_host((struct sockaddr *)&peer, peer_len, peer_str, sizeof peer_str);

        // szukamy pakietu NS (IPv4: za nagłówkiem IP, IPv6: od początku)
        size_t pkt_len;
        const uint8_t *pkt = ns_locate(family, buf, (size_t)n, &pkt_len);
        if (pkt == NULL)
            continue;

        // sprawdzamy magic, wersję, długość i sumę kontrolną
        const char *err = ns_validate(pkt, pkt_len);
        if (err != NULL) {
            printf("[RAW][srv] odrzucony pakiet od %s: %s\n", peer_str, err);
            continue;
        }

        const struct ns_hdr *h = (const struct ns_hdr *)pkt;

        // obsługujemy tylko żądania (gniazdo widzi też nasze własne odpowiedzi)
        if (h->type != NS_TYPE_REQ)
            continue;

        uint16_t id = ntohs(h->id);
        uint16_t seq = ntohs(h->seq);
        uint16_t data_len = ntohs(h->length);
        const uint8_t *data = pkt + sizeof *h;

        printf("[RAW][srv] REQ od %s id=%u seq=%u, %u B: \"%.*s\"\n",
               peer_str, (unsigned)id, (unsigned)seq, (unsigned)data_len,
               (int)data_len, (const char *)data);

        if (family == AF_INET) {
            print_ipv4_header((const struct iphdr *)buf);
        } else {
            // nagłówka IPv6 nie ma w buforze – zdjęło go jądro
            printf("[RAW][srv]   IPv6: nagłówek zdjęty przez jądro, dane NS od bajtu 0 "
                   "(%zd B w buforze)\n", n);
        }

        if (data_len > NS_MAX_DATA) {
            printf("[RAW][srv]   za dużo danych – nie odpowiadam\n");
            continue;
        }

        // budujemy odpowiedź REP (te same id, seq i dane) i odsyłamy ją nadawcy
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
