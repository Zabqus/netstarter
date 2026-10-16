/*
 * udp_client.c – klient echo UDP z timeoutem odpowiedzi.
 *
 * Uruchomienie:  ./build/udp_client [-4|-6] -h <host> [-p <port>] [-m "wiadomość"]
 *   bez -4/-6 – dowolna wersja IP, -m – jeden datagram i koniec (kod 1 przy timeoucie),
 *   bez -m    – każda linia ze stdin to osobny datagram (Ctrl+D kończy).
 *
 * Sekwencja wywołań:
 *   getaddrinfo → socket → setsockopt(SO_RCVTIMEO) → sendto / recvfrom → close
 *
 * UDP nie potwierdza dostarczenia – gdy datagram zginie (np. przez firewall),
 * po 2 s wypisujemy "brak odpowiedzi (timeout)" zamiast czekać w nieskończoność.
 */
#include <errno.h>       /* errno, EAGAIN, EWOULDBLOCK */
#include <netdb.h>       /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp, strlen */
#include <unistd.h>      /* close */
#include <sys/socket.h>  /* socket, setsockopt, sendto, recvfrom */
#include <sys/time.h>    /* struct timeval */

#include "net_common.h"

#define DEFAULT_PORT    "5001"
#define BUF_SIZE        1500
#define TIMEOUT_SECONDS 2

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4|-6] -h <host> [-p <port>] [-m \"wiadomość\"]\n"
                    "  -4 / -6    wymuś IPv4 / IPv6 (domyślnie: dowolna)\n"
                    "  -h <host>  adres lub nazwa serwera\n"
                    "  -p <port>  port serwera (domyślnie " DEFAULT_PORT ")\n"
                    "  -m <tekst> wyślij jedną wiadomość i zakończ\n", prog);
    exit(EXIT_FAILURE);
}


static int echo_once(int fd, const struct addrinfo *server, const char *text, size_t len)
{
    // wysyłamy datagram do serwera (port źródłowy przydziela jądro)
    if (sendto(fd, text, len, 0, server->ai_addr, server->ai_addrlen) < 0) {
        perror("sendto");
        return -1;
    }

    char reply[BUF_SIZE];
    struct sockaddr_storage from;
    socklen_t from_len = sizeof from;

    // czekamy na odpowiedź maks. TIMEOUT_SECONDS, potem EAGAIN
    ssize_t n = recvfrom(fd, reply, sizeof reply, 0, (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            printf("[UDP][cli] brak odpowiedzi (timeout %d s)\n", TIMEOUT_SECONDS);
        else
            perror("recvfrom");
        return -1;
    }

    char addr[ADDR_STR_LEN];
    printf("[UDP][cli] odpowiedź (%zd B) od %s: \"%.*s\"\n",
           n, addr_to_str((struct sockaddr *)&from, from_len, addr, sizeof addr),
           (int)text_len(reply, (size_t)n), reply);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *host = NULL;
    const char *port = DEFAULT_PORT;
    const char *message = NULL;
    int family = AF_UNSPEC;

    for (int i = 1; i < argc; i++) {
        int f = parse_family(argv[i]);
        if (f != -1)
            family = f;
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
            host = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            message = argv[++i];
        else
            usage(argv[0]);
    }
    if (host == NULL)
        usage(argv[0]);

    setvbuf(stdout, NULL, _IOLBF, 0);

    // zamieniamy nazwę/adres i port na adres serwera
    struct addrinfo hints;
    struct addrinfo *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;                 /* AF_UNSPEC = obie wersje IP */
    hints.ai_socktype = SOCK_DGRAM;           /* UDP */
    hints.ai_flags = AI_NUMERICSERV;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(rc));
        return EXIT_FAILURE;
    }
    // bierzemy pierwszy adres – w UDP nie da się "sprawdzić" połączenia jak w TCP

    // tworzymy gniazdo UDP
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
        die("socket");

    // ustawiamy timeout odbioru
    struct timeval tv = { .tv_sec = TIMEOUT_SECONDS, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0)
        die("setsockopt(SO_RCVTIMEO)");

    char addr[ADDR_STR_LEN];
    printf("[UDP][cli] serwer: %s\n", addr_to_str(res->ai_addr, res->ai_addrlen, addr, sizeof addr));

    char line[BUF_SIZE];
    int status = 0;

    if (message != NULL) {
        // jedna wiadomość, z dopisanym '\n'
        int len = snprintf(line, sizeof line, "%s\n", message);
        if (len >= (int)sizeof line)
            len = (int)sizeof line - 1;   /* za długa – obcinamy */
        if (echo_once(fd, res, line, (size_t)len) != 0)
            status = 1;
    } else {
        // po timeoucie nie kończymy – można wysłać kolejną linię
        printf("[UDP][cli] wpisuj linie, Ctrl+D kończy\n");
        while (fgets(line, sizeof line, stdin) != NULL)
            echo_once(fd, res, line, strlen(line));
    }

    freeaddrinfo(res);
    close(fd);   /* zwalniamy gniazdo (w UDP nie ma połączenia do zamknięcia) */
    return status;
}
