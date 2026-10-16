/*
 * net_common.c – implementacja wspólnych helperów (opis w net_common.h).
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

    // zamieniamy adres i port na tekst (liczbowo, bez zapytań DNS) – działa dla IPv4 i IPv6
    int rc = getnameinfo(sa, len, host, sizeof host, port, sizeof port,
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        snprintf(buf, n, "<nieznany adres: %s>", gai_strerror(rc));
        return buf;
    }

    // IPv6 w nawiasach, bo sam adres zawiera dwukropki
    if (sa->sa_family == AF_INET6)
        snprintf(buf, n, "[%s]:%s", host, port);
    else
        snprintf(buf, n, "%s:%s", host, port);

    return buf;
}

const char *addr_to_host(const struct sockaddr *sa, socklen_t len, char *buf, size_t n)
{
    // to samo co wyżej, ale bez portu
    int rc = getnameinfo(sa, len, buf, (socklen_t)n, NULL, 0, NI_NUMERICHOST);
    if (rc != 0)
        snprintf(buf, n, "<nieznany adres: %s>", gai_strerror(rc));
    return buf;
}

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;   /* pierwszy niewysłany bajt */
    size_t left = len;     /* ile zostało do wysłania */

    while (left > 0) {
        // wysyłamy resztę danych (send() może przyjąć mniej, niż prosiliśmy)
        // MSG_NOSIGNAL: przy zamkniętym połączeniu błąd zamiast zabicia programu
        ssize_t sent = send(fd, p, left, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR)   /* przerwane sygnałem – ponawiamy */
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
    uint32_t sum = 0;   /* 32 bity, żeby nie gubić przeniesień */

    // 1. sumujemy dane jako słowa 16-bitowe (starszy bajt pierwszy)
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }

    // 2. nieparzysta długość – ostatni bajt dopełniamy zerem
    if (len == 1)
        sum += (uint32_t)(p[0] << 8);

    // 3. przeniesienia powyżej 16 bitów dodajemy z powrotem
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    // 4. wynik to negacja sumy
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
