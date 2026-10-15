/*
 * raw_proto.h – własny protokół warstwy 3 "NS" (netstarter), wspólny dla raw_client i raw_server.
 *
 * Pakiet NS jest przenoszony bezpośrednio w IP, z numerem protokołu 253
 * (pole "protocol" w IPv4 / "next header" w IPv6). Numery 253 i 254 są zarezerwowane
 * do eksperymentów (RFC 3692), więc jądro ich nie obsługuje – nie ma tu portów, TCP ani UDP,
 * całą logikę (nagłówek, suma kontrolna, dopasowanie odpowiedzi) robimy sami.
 *
 * Budowa pakietu (za nagłówkiem IP):
 *
 *   0               1               2               3
 *   +---------------+---------------+---------------+---------------+
 *   |          magic = "NS"         |    version    |     type      |
 *   +---------------+---------------+---------------+---------------+
 *   |              id               |              seq              |
 *   +---------------+---------------+---------------+---------------+
 *   |            length             |           checksum            |
 *   +---------------+---------------+---------------+---------------+
 *   |                      dane (length bajtów) ...                 |
 *
 * Wszystkie pola wielobajtowe w network byte order (big-endian) – htons()/ntohs().
 */
#ifndef RAW_PROTO_H
#define RAW_PROTO_H

#include <errno.h>       /* errno, EPERM, EACCES */
#include <stdint.h>      /* uint8_t, uint16_t */
#include <stdio.h>       /* fprintf */
#include <stdlib.h>      /* exit */
#include <string.h>      /* memcpy */
#include <arpa/inet.h>   /* htons, ntohs */
#include <sys/socket.h>  /* socket, SOCK_RAW */

#include "net_common.h"  /* inet_checksum, die */

#define NS_PROTO     253         /* numer protokołu w nagłówku IP */
#define NS_MAGIC     0x4E53      /* "NS" – pozwala odróżnić nasze pakiety od przypadkowych */
#define NS_VERSION   1
#define NS_TYPE_REQ  1           /* żądanie (klient → serwer) */
#define NS_TYPE_REP  2           /* odpowiedź (serwer → klient) */
#define NS_MAX_DATA  1024        /* maksymalna długość danych w jednym pakiecie */

struct ns_hdr {
    uint16_t magic;      /* NS_MAGIC, network byte order */
    uint8_t  version;    /* 1 */
    uint8_t  type;       /* REQ / REP */
    uint16_t id;         /* identyfikator klienta (getpid() & 0xFFFF) */
    uint16_t seq;        /* numer kolejny */
    uint16_t length;     /* długość danych po nagłówku */
    uint16_t checksum;   /* RFC 1071 po nagłówku + danych, liczona z checksum = 0 */
} __attribute__((packed));
/*
 * packed: kompilator nie wstawi żadnych bajtów wyrównania – struktura w pamięci
 * odpowiada dokładnie bajtom na kablu. Sprawdzamy to przy kompilacji:
 */
_Static_assert(sizeof(struct ns_hdr) == 12, "nagłówek NS musi mieć 12 bajtów");

/*
 * Buduje kompletny pakiet NS w buforze buf (min. sizeof(struct ns_hdr) + len bajtów).
 * Zwraca łączną długość pakietu.
 */
static inline size_t ns_build(uint8_t *buf, uint8_t type, uint16_t id, uint16_t seq,
                              const void *data, size_t len)
{
    struct ns_hdr *h = (struct ns_hdr *)buf;   /* nagłówek leży na początku bufora */

    h->magic    = htons(NS_MAGIC);
    h->version  = NS_VERSION;
    h->type     = type;
    h->id       = htons(id);
    h->seq      = htons(seq);
    h->length   = htons((uint16_t)len);
    h->checksum = 0;                            /* przy liczeniu sumy pole musi być zerem */

    memcpy(buf + sizeof *h, data, len);         /* dane zaraz za nagłówkiem */

    /* Suma po nagłówku + danych; inet_checksum zwraca porządek hosta → htons. */
    size_t total = sizeof *h + len;
    h->checksum = htons(inet_checksum(buf, total));
    return total;
}

/*
 * Sprawdza, czy pod pkt (len bajtów) leży poprawny pakiet NS.
 * Zwraca NULL gdy wszystko OK, w przeciwnym razie krótki opis błędu (do logu).
 */
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

    /*
     * Weryfikacja RFC 1071: suma po całym pakiecie RAZEM z polem checksum daje 0
     * (w zapisie ~suma), jeśli żaden bit się nie zmienił.
     */
    if (inet_checksum(pkt, total) != 0)
        return "zła suma kontrolna";

    return NULL;
}

/*
 * Otwiera gniazdo surowe protokołu NS dla danej rodziny (AF_INET / AF_INET6).
 * Gniazda surowe wymagają CAP_NET_RAW – bez niej socket() zwraca EPERM.
 */
static inline int ns_open_socket(int family)
{
    /*
     * SOCK_RAW + numer protokołu 253: jądro przekaże nam każdy przychodzący pakiet IP
     * z tym numerem protokołu, a przy wysyłaniu samo dołoży nagłówek IP z protocol = 253.
     */
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
