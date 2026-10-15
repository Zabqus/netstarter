/*
 * net_common.c – implementacja wspólnych helperów (opis funkcji w net_common.h).
 */
#include "net_common.h"

#include <errno.h>       /* errno, EINTR */
#include <netdb.h>       /* getnameinfo, gai_strerror, NI_* */
#include <stdio.h>       /* perror, snprintf */
#include <stdlib.h>      /* exit */
#include <string.h>      /* strcmp */
#include <netinet/in.h>  /* AF_INET, AF_INET6 */

void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

const char *addr_to_str(const struct sockaddr *sa, socklen_t len, char *buf, size_t n)
{
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];

    /*
     * getnameinfo() działa dla IPv4 i IPv6 – sama sprawdza sa->sa_family.
     * NI_NUMERICHOST / NI_NUMERICSERV: chcemy liczb (adres, numer portu),
     * a nie nazw z DNS czy /etc/services – szybciej i bez zapytań do sieci.
     * Zwraca 0 przy sukcesie albo kod błędu dla gai_strerror() (nie errno!).
     */
    int rc = getnameinfo(sa, len, host, sizeof host, port, sizeof port,
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        snprintf(buf, n, "<nieznany adres: %s>", gai_strerror(rc));
        return buf;
    }

    /* IPv6 w nawiasach, bo sam adres zawiera dwukropki i port by się zlał. */
    if (sa->sa_family == AF_INET6)
        snprintf(buf, n, "[%s]:%s", host, port);
    else
        snprintf(buf, n, "%s:%s", host, port);

    return buf;
}

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;   /* wskaźnik na pierwszy jeszcze niewysłany bajt */
    size_t left = len;     /* ile bajtów zostało do wysłania */

    while (left > 0) {
        /*
         * send() kopiuje dane do bufora nadawczego jądra i zwraca, ile przyjął –
         * może to być mniej niż left (np. gdy bufor jest prawie pełny).
         * MSG_NOSIGNAL: gdy druga strona zamknęła połączenie, dostaniemy błąd EPIPE
         * zamiast sygnału SIGPIPE, który domyślnie zabiłby cały proces.
         */
        ssize_t sent = send(fd, p, left, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR)   /* przerwane przez sygnał – po prostu ponawiamy */
                continue;
            return -1;
        }
        p += sent;
        left -= (size_t)sent;
    }
    return 0;
}

uint16_t inet_checksum(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t sum = 0;   /* 32 bity, żeby przeniesienia z dodawania 16-bitowego się nie gubiły */

    /*
     * 1. Sumujemy dane jako kolejne słowa 16-bitowe.
     *    Słowo składamy ręcznie z dwóch bajtów (starszy pierwszy = network byte order),
     *    dzięki temu wynik nie zależy od architektury ani wyrównania w pamięci.
     */
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }

    /* 2. Nieparzysta długość: ostatni bajt traktujemy jak słowo uzupełnione zerem. */
    if (len == 1)
        sum += (uint32_t)(p[0] << 8);

    /*
     * 3. Zawijanie przeniesień: wszystko powyżej 16 bitów dodajemy z powrotem
     *    do młodszych 16 bitów (dodawanie w arytmetyce uzupełnień do jedności).
     */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    /* 4. Wynikiem jest negacja bitowa sumy (uzupełnienie do jedności). */
    return (uint16_t)~sum;
}

int parse_family(const char *flag)
{
    if (strcmp(flag, "-4") == 0)
        return AF_INET;
    if (strcmp(flag, "-6") == 0)
        return AF_INET6;
    return -1;
}

size_t text_len(const char *buf, size_t n)
{
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        n--;
    return n;
}
