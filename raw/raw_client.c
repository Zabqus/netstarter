/*
 * raw_client.c – klient echo na gniazdach surowych (warstwa 3), protokół NS (IP proto 253).
 *
 * Uruchomienie (wymaga CAP_NET_RAW):
 *   sudo ./build/raw_client [-4|-6] -h <host> [-m "wiadomość"]   albo bez sudo po scripts/setcap.sh
 *   -m – jedno żądanie i koniec (kod 1 przy braku odpowiedzi),
 *   bez -m – każda linia ze stdin to osobne żądanie (Ctrl+D kończy).
 *
 * Sekwencja wywołań:
 *   socket(AF_INET/AF_INET6, SOCK_RAW, 253) → getaddrinfo → setsockopt(SO_RCVTIMEO) →
 *   [budowa pakietu NS] → sendto → recvfrom (aż przyjdzie nasz REP) → close
 *
 * Gniazdo surowe widzi każdy pakiet protokołu 253, więc przyjmujemy tylko REP z naszym id i seq.
 */
#include <errno.h>       /* errno, EAGAIN, EWOULDBLOCK, EINTR */
#include <netdb.h>       /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp, strlen */
#include <time.h>        /* clock_gettime – pomiar RTT */
#include <unistd.h>      /* close, getpid */
#include <arpa/inet.h>   /* ntohs */
#include <netinet/ip.h>  /* struct iphdr – nagłówek IPv4 */
#include <sys/socket.h>  /* sendto, recvfrom, setsockopt */
#include <sys/time.h>    /* struct timeval */

#include "net_common.h"
#include "raw_proto.h"

#define RECV_BUF_SIZE   65536
#define TIMEOUT_SECONDS 2

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4|-6] -h <host> [-m \"wiadomość\"]\n"
                    "  -4 / -6    IPv4 (domyślnie) / IPv6\n"
                    "  -h <host>  adres lub nazwa serwera\n"
                    "  -m <tekst> wyślij jedno żądanie i zakończ\n"
                    "Wymaga CAP_NET_RAW: sudo albo scripts/setcap.sh\n", prog);
    exit(EXIT_FAILURE);
}

/* Ile milisekund minęło od start (zegar monotoniczny). */
static double elapsed_ms(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0
         + (double)(now.tv_nsec - start->tv_nsec) / 1e6;
}

/* Wysyła żądanie REQ i czeka na pasującą odpowiedź REP. 0 – przyszła, -1 – timeout/błąd. */
static int ping_once(int fd, int family, const struct sockaddr *dst, socklen_t dst_len,
                     uint16_t id, uint16_t seq, const char *text, size_t len)
{
    uint8_t pkt[sizeof(struct ns_hdr) + NS_MAX_DATA];
    uint8_t buf[RECV_BUF_SIZE];
    char from_str[ADDR_STR_LEN];

    if (len > NS_MAX_DATA)
        len = NS_MAX_DATA;   /* za długa – obcinamy */

    // budujemy pakiet NS: nagłówek + dane + suma kontrolna
    size_t pkt_len = ns_build(pkt, NS_TYPE_REQ, id, seq, text, len);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // wysyłamy pakiet NS – nagłówek IP (adresy, TTL, protokół 253) dokłada jądro
    if (sendto(fd, pkt, pkt_len, 0, dst, dst_len) < 0) {
        perror("sendto");
        return -1;
    }
    printf("[RAW][cli] REQ id=%u seq=%u, %zu B danych\n", (unsigned)id, (unsigned)seq, len);

    for (;;) {
        struct sockaddr_storage from;
        socklen_t from_len = sizeof from;

        /* czekamy na pakiet maks. TIMEOUT_SECONDS */
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[RAW][cli] brak odpowiedzi (timeout %d s)\n", TIMEOUT_SECONDS);
                return -1;
            }
            perror("recvfrom");
            return -1;
        }
        double rtt = elapsed_ms(&start);

        // szukamy pakietu NS (IPv4: za nagłówkiem IP, IPv6: od początku)
        size_t ns_len;
        const uint8_t *ns = ns_locate(family, buf, (size_t)n, &ns_len);
        const struct ns_hdr *h = (const struct ns_hdr *)ns;

        // nasz pakiet = poprawny REP z naszym id i seq
        int ours = ns != NULL && ns_validate(ns, ns_len) == NULL
                   && h->type == NS_TYPE_REP && ntohs(h->id) == id && ntohs(h->seq) == seq;

        if (!ours) {
            /* obcy pakiet – pomijamy, ale pilnujemy łącznego czasu czekania */
            if (rtt > TIMEOUT_SECONDS * 1000.0) {
                printf("[RAW][cli] brak odpowiedzi (timeout %d s)\n", TIMEOUT_SECONDS);
                return -1;
            }
            continue;
        }

        uint16_t data_len = ntohs(h->length);
        const char *data = (const char *)ns + sizeof *h;
        addr_to_host((struct sockaddr *)&from, from_len, from_str, sizeof from_str);

        printf("[RAW][cli] REP od %s id=%u seq=%u, %u B: \"%.*s\"  RTT=%.3f ms\n",
               from_str, (unsigned)id, (unsigned)seq,
               (unsigned)data_len, (int)data_len, data, rtt);

        /* TTL znamy tylko w IPv4 – tam nagłówek IP jest w buforze */
        if (family == AF_INET)
            printf("[RAW][cli]   IPv4: ttl=%u\n", (unsigned)((const struct iphdr *)buf)->ttl);
        return 0;
    }
}

int main(int argc, char *argv[])
{
    int family = AF_INET;
    const char *host = NULL;
    const char *message = NULL;

    for (int i = 1; i < argc; i++) {
        int f = parse_family(argv[i]);
        if (f != -1)
            family = f;
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
            host = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            message = argv[++i];
        else
            usage(argv[0]);
    }
    if (host == NULL)
        usage(argv[0]);

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* tworzymy gniazdo surowe (najpierw – bez uprawnień od razu komunikat) */
    int fd = ns_open_socket(family);

    /* zamieniamy nazwę/adres serwera na sockaddr (bez portu) */
    struct addrinfo hints;
    struct addrinfo *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = NS_PROTO;

    int rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(rc));
        return EXIT_FAILURE;
    }

    /* ustawiamy timeout odbioru */
    struct timeval tv = { .tv_sec = TIMEOUT_SECONDS, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0)
        die("setsockopt(SO_RCVTIMEO)");

    /* identyfikator klienta – PID obcięty do 16 bitów (jak w ping) */
    uint16_t id = (uint16_t)(getpid() & 0xFFFF);
    uint16_t seq = 1;

    char addr[ADDR_STR_LEN];
    printf("[RAW][cli] serwer: %s, protokół %d, id=%u\n",
           addr_to_host(res->ai_addr, res->ai_addrlen, addr, sizeof addr),
           NS_PROTO, (unsigned)id);

    int status = 0;
    if (message != NULL) {
        if (ping_once(fd, family, res->ai_addr, res->ai_addrlen,
                      id, seq, message, strlen(message)) != 0)
            status = 1;
    } else {
        printf("[RAW][cli] wpisuj linie, Ctrl+D kończy\n");
        char line[NS_MAX_DATA + 2];
        while (fgets(line, sizeof line, stdin) != NULL) {
            /* wysyłamy linię bez końcowego '\n' */
            ping_once(fd, family, res->ai_addr, res->ai_addrlen,
                      id, seq, line, text_len(line, strlen(line)));
            seq++;
        }
    }

    freeaddrinfo(res);
    close(fd);
    return status;
}
