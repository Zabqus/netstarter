/*
 * raw_proto.h – własny protokół warstwy 3 "NS" (IP proto 253), wspólny dla raw_client i raw_server.
 *
 * Protokół 253 jest zarezerwowany do eksperymentów (RFC 3692) – jądro go nie obsługuje,
 * więc nagłówek, sumę kontrolną i dopasowanie odpowiedzi robimy sami.
 *
 * Pakiet NS (za nagłówkiem IP), pola w network byte order:
 *
 *   +---------------+---------------+---------------+---------------+
 *   |          magic = "NS"         |    version    |     type      |
 *   +---------------+---------------+---------------+---------------+
 *   |              id               |              seq              |
 *   +---------------+---------------+---------------+---------------+
 *   |            length             |           checksum            |
 *   +---------------+---------------+---------------+---------------+
 *   |                      dane (length bajtów) ...                 |
 */
#ifndef RAW_PROTO_H
#define RAW_PROTO_H

#include <errno.h>       /* errno, EPERM, EACCES */
#include <stdint.h>      /* uint8_t, uint16_t */
#include <stdio.h>       /* fprintf */
#include <stdlib.h>      /* exit */
#include <string.h>      /* memcpy */
#include <arpa/inet.h>   /* htons, ntohs */
#include <netinet/ip.h>  /* struct iphdr – nagłówek IPv4 */
#include <sys/socket.h>  /* socket, SOCK_RAW, AF_INET6 */

#include "net_common.h"  /* inet_checksum, die */

#define NS_PROTO     253         /* numer protokołu w nagłówku IP */
#define NS_MAGIC     0x4E53      /* "NS" – odróżnia nasze pakiety od obcych */
#define NS_VERSION   1
#define NS_TYPE_REQ  1           /* żądanie (klient → serwer) */
#define NS_TYPE_REP  2           /* odpowiedź (serwer → klient) */
#define NS_MAX_DATA  1024        /* maks. długość danych w pakiecie */

struct ns_hdr {
    uint16_t magic;      /* NS_MAGIC */
    uint8_t  version;    /* 1 */
    uint8_t  type;       /* REQ / REP */
    uint16_t id;         /* identyfikator klienta (PID) */
    uint16_t seq;        /* numer kolejny */
    uint16_t length;     /* długość danych */
    uint16_t checksum;   /* RFC 1071 po nagłówku + danych */
} __attribute__((packed));
/* packed – bez bajtów wyrównania, struktura = dokładnie 12 bajtów na kablu */
_Static_assert(sizeof(struct ns_hdr) == 12, "nagłówek NS musi mieć 12 bajtów");

// Buduje pakiet NS (nagłówek + dane) w buf. Zwraca jego długość.
static inline size_t ns_build(uint8_t *buf, uint8_t type, uint16_t id, uint16_t seq,
                              const void *data, size_t len)
{
    struct ns_hdr *h = (struct ns_hdr *)buf;

    /* wypełniamy nagłówek (pola wielobajtowe przez htons) */
    h->magic    = htons(NS_MAGIC);
    h->version  = NS_VERSION;
    h->type     = type;
    h->id       = htons(id);
    h->seq      = htons(seq);
    h->length   = htons((uint16_t)len);
    h->checksum = 0;                            /* przy liczeniu sumy musi być 0 */

    /* dane wstawiamy zaraz za nagłówkiem */
    memcpy(buf + sizeof *h, data, len);

    /* liczymy sumę kontrolną po całym pakiecie i wpisujemy ją do nagłówka */
    size_t total = sizeof *h + len;
    h->checksum = htons(inet_checksum(buf, total));
    return total;
}

/* Sprawdza pakiet NS. NULL – poprawny, inaczej opis błędu. */
static inline const char *ns_validate(const uint8_t *pkt, size_t len)
{
    if (len < sizeof(struct ns_hdr))
        return "za krótki pakiet";

    const struct ns_hdr *h = (const struct ns_hdr *)pkt;
    if (ntohs(h->magic) != NS_MAGIC)
        return "zły magic";
    if (h->version != NS_VERSION)
        return "nieznana wersja";

    size_t total = sizeof *h + ntohs(h->length);
    if (total > len)
        return "pole length większe niż pakiet";

    /* suma po pakiecie razem z polem checksum daje 0, jeśli nic się nie zmieniło */
    if (inet_checksum(pkt, total) != 0)
        return "zła suma kontrolna";

    return NULL;
}

/*
 * Znajduje początek pakietu NS w buforze z recvfrom():
 *   IPv4 – jądro oddaje pakiet RAZEM z nagłówkiem IP, pomijamy ihl * 4 bajtów,
 *   IPv6 – jądro zdejmuje nagłówek IPv6, dane NS są od początku bufora.
 */
static inline const uint8_t *ns_locate(int family, const uint8_t *buf, size_t n, size_t *len)
{
    if (family == AF_INET6) {
        *len = n;
        return buf;
    }

    if (n < sizeof(struct iphdr))
        return NULL;
    const struct iphdr *ip = (const struct iphdr *)buf;
    size_t ip_hlen = (size_t)ip->ihl * 4;   /* ihl – długość nagłówka w słowach 32-bit */
    if (ip_hlen < sizeof(struct iphdr) || ip_hlen > n)
        return NULL;

    *len = n - ip_hlen;
    return buf + ip_hlen;
}

/* Tworzy gniazdo surowe protokołu 253 (wymaga CAP_NET_RAW). */
static inline int ns_open_socket(int family)
{
    /* gniazdo surowe: dostajemy każdy pakiet IP z protokołem 253, jądro dokłada nagłówek IP */
    int fd = socket(family, SOCK_RAW, NS_PROTO);
    if (fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr, "brak uprawnień do gniazd surowych – uruchom z sudo "
                            "lub nadaj cap_net_raw (scripts/setcap.sh)\n");
            exit(EXIT_FAILURE);
        }
        die("socket");
    }
    return fd;
}

#endif /* RAW_PROTO_H */
