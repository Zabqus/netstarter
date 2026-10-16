/*
 * tcp_client.c – klient echo TCP.
 *
 * Uruchomienie:  ./build/tcp_client [-4|-6] -h <host> [-p <port>] [-m "wiadomość"]
 *   bez -4/-6 – dowolna wersja IP, -m – jedna wiadomość i koniec,
 *   bez -m    – wysyła kolejne linie ze stdin (Ctrl+D kończy).
 * Przykłady:  -h 192.168.1.201   -h fd00:10::201   -h ::1
 *
 * Sekwencja wywołań:
 *   getaddrinfo → socket → connect → send_all / recv → close
 *
 * Klient nie robi bind() – adres i port źródłowy dobiera jądro przy connect().
 */
#include <netdb.h>       /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp, strlen */
#include <unistd.h>      /* close */
#include <sys/socket.h>  /* socket, connect, recv */

#include "net_common.h"

#define DEFAULT_PORT "5000"
#define BUF_SIZE     1024

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4|-6] -h <host> [-p <port>] [-m \"wiadomość\"]\n"
                    "  -4 / -6    wymuś IPv4 / IPv6 (domyślnie: dowolna)\n"
                    "  -h <host>  adres lub nazwa serwera\n"
                    "  -p <port>  port serwera (domyślnie " DEFAULT_PORT ")\n"
                    "  -m <tekst> wyślij jedną wiadomość i zakończ\n", prog);
    exit(EXIT_FAILURE);
}

/* Łączy się z host:port i zwraca połączone gniazdo. */
static int connect_to(int family, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;

    // zamieniamy nazwę/adres i port na listę adresów serwera
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;          /* TCP */
    hints.ai_flags = AI_NUMERICSERV;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    /* próbujemy kolejnych adresów, aż któryś się połączy */
    int fd = -1;
    char addr[ADDR_STR_LEN];
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        addr_to_str(ai->ai_addr, ai->ai_addrlen, addr, sizeof addr);

        // tworzymy gniazdo TCP tej samej rodziny co adres
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            perror("socket");
            continue;
        }

        // zestawiamy połączenie (SYN, SYN-ACK, ACK)
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            printf("[TCP][cli] połączono z %s\n", addr);
            break;
        }
        fprintf(stderr, "[TCP][cli] %s: ", addr);
        perror("connect");
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "[TCP][cli] nie udało się połączyć z %s:%s\n", host, port);
        exit(EXIT_FAILURE);
    }
    return fd;
}

/*
 * Odbiera dokładnie len bajtów – TCP to strumień, odpowiedź może przyjść w kawałkach.
 * 0 – OK, 1 – serwer zamknął połączenie, -1 – błąd.
 */
static int recv_exact(int fd, char *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n < 0) {
            perror("recv");
            return -1;
        }
        if (n == 0)   /* serwer zamknął połączenie */
            return 1;
        got += (size_t)n;
    }
    return 0;
}

/* Wysyła tekst i wypisuje echo. 0 – OK, inna wartość – koniec. */
static int echo_once(int fd, const char *text, size_t len)
{
    char reply[BUF_SIZE];

    // wysyłamy wiadomość
    if (send_all(fd, text, len) < 0) {
        perror("send");
        return -1;
    }

    /* odbieramy tyle samo bajtów z powrotem */
    int rc = recv_exact(fd, reply, len);
    if (rc == 1)
        printf("[TCP][cli] serwer zamknął połączenie\n");
    if (rc != 0)
        return rc;

    printf("[TCP][cli] odpowiedź (%zu B): \"%.*s\"\n",
           len, (int)text_len(reply, len), reply);
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

    int fd = connect_to(family, host, port);
    char line[BUF_SIZE];

    if (message != NULL) {
        /* jedna wiadomość, z dopisanym '\n' */
        int len = snprintf(line, sizeof line, "%s\n", message);
        if (len >= (int)sizeof line)
            len = (int)sizeof line - 1;   /* za długa – obcinamy */
        echo_once(fd, line, (size_t)len);
    } else {
        /* kolejne linie ze stdin, Ctrl+D kończy */
        printf("[TCP][cli] wpisuj linie, Ctrl+D kończy\n");
        while (fgets(line, sizeof line, stdin) != NULL) {
            if (echo_once(fd, line, strlen(line)) != 0)
                break;
        }
    }

    /* zamykamy połączenie (FIN) – serwer dostanie recv() == 0 */
    close(fd);
    return 0;
}
