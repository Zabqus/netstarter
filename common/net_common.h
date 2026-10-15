/*
 * net_common.h – wspólne funkcje pomocnicze dla wszystkich programów netstarter.
 *
 * Implementacja w net_common.c, kompilowana do biblioteki statycznej "netcommon",
 * z którą linkowany jest każdy klient i serwer.
 */
#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <stddef.h>      /* size_t */
#include <stdint.h>      /* uint16_t */
#include <sys/socket.h>  /* struct sockaddr, socklen_t */

/*
 * Rozmiar bufora na tekst "host:port" / "[host]:port".
 * Najdłuższy adres IPv6 to 45 znaków (INET6_ADDRSTRLEN = 46 z '\0'),
 * do tego nawiasy, dwukropek i port (max 5 cyfr) – 64 B wystarcza z zapasem.
 */
#define ADDR_STR_LEN 64

/*
 * Wypisuje komunikat błędu (perror – dokleja opis z errno) i kończy program.
 * Używać tylko po wywołaniach, które ustawiają errno (socket, bind, ...).
 * _Noreturn mówi kompilatorowi, że funkcja nigdy nie wraca.
 */
_Noreturn void die(const char *msg);

/*
 * Zamienia adres gniazda (IPv4 lub IPv6) na czytelny tekst:
 *   IPv4: "192.168.1.202:43122"
 *   IPv6: "[fd00:10::202]:43122"
 * Wynik zapisuje do buf (rozmiar n, najlepiej ADDR_STR_LEN) i zwraca buf,
 * żeby można było użyć funkcji bezpośrednio w printf.
 */
const char *addr_to_str(const struct sockaddr *sa, socklen_t len, char *buf, size_t n);

/*
 * Jak addr_to_str(), ale tylko sam adres, bez portu ("192.168.1.202", "fd00:10::202").
 * Dla gniazd surowych – warstwa 3 nie ma pojęcia portu.
 */
const char *addr_to_host(const struct sockaddr *sa, socklen_t len, char *buf, size_t n);

/*
 * Wysyła cały bufor przez gniazdo TCP. send() może wysłać mniej bajtów,
 * niż prosiliśmy (częściowy zapis), więc ponawiamy aż do skutku.
 * Zwraca 0 gdy wysłano wszystko, -1 przy błędzie (errno ustawione).
 */
int send_all(int fd, const void *buf, size_t len);

/*
 * Suma kontrolna Internetu (RFC 1071) – ta sama, której używają IP, ICMP, TCP, UDP.
 * Zwraca wynik w porządku bajtów HOSTA – przed wpisaniem do nagłówka użyj htons().
 * Weryfikacja: suma policzona po całym pakiecie (razem z polem checksum) daje 0.
 */
uint16_t inet_checksum(const void *data, size_t len);

/*
 * Zamienia flagę wiersza poleceń na rodzinę adresów:
 *   "-4" -> AF_INET, "-6" -> AF_INET6, cokolwiek innego -> -1.
 */
int parse_family(const char *flag);

/*
 * Zwraca długość tekstu w buf (n bajtów) bez końcowych znaków '\n' i '\r'.
 * Tylko do ładnego logowania: printf("%.*s", (int)text_len(buf, n), buf).
 */
size_t text_len(const char *buf, size_t n);

/*
 * Zasada najmniejszych uprawnień: zeruje wszystkie capabilities procesu
 * (zbiory effective, permitted i inheritable) – bezpośrednio wywołaniem systemowym
 * capset, bez libcap. Serwery wołają ją po utworzeniu i zbindowaniu gniazda:
 * uprawnienia sprawdzane są tylko przy socket()/bind(), gotowe gniazdo działa dalej.
 * Zwraca 0 przy sukcesie, -1 przy błędzie (errno ustawione).
 */
int drop_capabilities(void);

#endif /* NET_COMMON_H */
