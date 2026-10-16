/*
 * net_common.h – wspólne funkcje pomocnicze dla wszystkich programów.
 * Implementacja w net_common.c (biblioteka statyczna "netcommon").
 */
#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <stddef.h>      /* size_t */
#include <stdint.h>      /* uint16_t */
#include <sys/socket.h>  /* struct sockaddr, socklen_t */

// Bufor na tekst "host:port" / "[host]:port" – najdłuższy adres IPv6 ma 45 znaków.
#define ADDR_STR_LEN 64

// Wypisuje błąd (perror) i kończy program.
_Noreturn void die(const char *msg);

// Adres gniazda jako tekst: "192.168.1.202:43122" albo "[fd00:10::202]:43122".
const char *addr_to_str(const struct sockaddr *sa, socklen_t len, char *buf, size_t n);

// Sam adres, bez portu – dla gniazd surowych (warstwa 3 nie ma portów).
const char *addr_to_host(const struct sockaddr *sa, socklen_t len, char *buf, size_t n);

// Wysyła cały bufor przez TCP (send() może wysłać tylko część). 0 – OK, -1 – błąd.
int send_all(int fd, const void *buf, size_t len);

// Suma kontrolna Internetu (RFC 1071). Wynik w porządku hosta – do nagłówka przez htons().
uint16_t inet_checksum(const void *data, size_t len);

// "-4" → AF_INET, "-6" → AF_INET6, inne → -1.
int parse_family(const char *flag);

// Długość tekstu bez końcowych '\n' / '\r' – do ładnego logowania.
size_t text_len(const char *buf, size_t n);

#endif /* NET_COMMON_H */
