/*
 * udp_server.c – serwer echo UDP.
 *
 * Uruchomienie:
 *   ./build/udp_server [-4] [-p <port>]       (domyślnie: IPv4, port 5001)
 *
 * Test bez klienta:  nc -u 127.0.0.1 5001   (wpisana linia wraca z powrotem)
 *
 * Sekwencja wywołań:
 *   getaddrinfo → socket → setsockopt(SO_REUSEADDR) → bind →
 *   recvfrom / sendto (pętla) → close
 *
 * UDP jest bezpołączeniowy: nie ma listen() ani accept(). Jedno gniazdo obsługuje
 * wszystkich klientów – adres nadawcy dostajemy przy każdym datagramie z recvfrom()
 * i na ten adres odsyłamy odpowiedź przez sendto().
 * Ctrl+C (SIGINT) kończy serwer i zamyka gniazdo.
 */
#include <errno.h>       /* errno, EINTR */
#include <netdb.h>       /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <signal.h>      /* sigaction, sig_atomic_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp */
#include <unistd.h>      /* close */
#include <sys/socket.h>  /* socket, setsockopt, bind, recvfrom, sendto */

#include "net_common.h"

#define DEFAULT_PORT "5001"
#define BUF_SIZE     1500   /* typowe MTU Ethernetu – większe datagramy i tak są rzadkie */

/* Jedyna zmienna globalna: flaga ustawiana przez obsługę SIGINT. */
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4] [-p <port>]\n"
                    "  -4         IPv4 (domyślnie)\n"
                    "  -p <port>  port nasłuchu (domyślnie " DEFAULT_PORT ")\n", prog);
    exit(EXIT_FAILURE);
}

/* Tworzy gniazdo UDP zbindowane na wszystkich adresach danej rodziny i zadanym porcie. */
static int create_udp_socket(int family, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;

    /* Tak samo jak w TCP, tylko SOCK_DGRAM zamiast SOCK_STREAM. */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;           /* UDP */
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    /* socket(): gniazdo datagramowe. */
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
        die("socket");

    /*
     * SO_REUSEADDR: UDP nie ma stanu TIME_WAIT, ale ustawiamy dla spójności z TCP –
     * pozwala zbindować port, nawet jeśli coś go jeszcze chwilowo trzyma.
     */
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        die("setsockopt(SO_REUSEADDR)");

    /* bind(): od teraz jądro kieruje do tego gniazda datagramy na nasz port. */
    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0)
        die("bind");

    freeaddrinfo(res);
    return fd;
}

int main(int argc, char *argv[])
{
    int family = AF_INET;
    const char *port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-4") == 0)
            family = AF_INET;
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = argv[++i];
        else
            usage(argv[0]);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Ctrl+C bez SA_RESTART – recvfrom() zwróci -1 z EINTR i pętla się zakończy. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        die("sigaction");

    int fd = create_udp_socket(family, port);

    struct sockaddr_storage local;
    socklen_t local_len = sizeof local;
    char addr[ADDR_STR_LEN];
    if (getsockname(fd, (struct sockaddr *)&local, &local_len) < 0)
        die("getsockname");
    printf("[UDP][srv] nasłuchuję na %s (Ctrl+C kończy)\n",
           addr_to_str((struct sockaddr *)&local, local_len, addr, sizeof addr));

    char buf[BUF_SIZE];
    while (!g_stop) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof peer;

        /*
         * recvfrom(): czeka na JEDEN datagram i zapisuje adres nadawcy do peer.
         * UDP zachowuje granice wiadomości: jeden sendto() u klienta = jeden
         * recvfrom() tutaj (w przeciwieństwie do strumienia TCP).
         * Jeśli datagram jest większy niż bufor, nadmiar jest po cichu ucinany.
         */
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&peer, &peer_len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recvfrom");
            continue;
        }

        addr_to_str((struct sockaddr *)&peer, peer_len, addr, sizeof addr);
        printf("[UDP][srv] %zd B od %s: \"%.*s\"\n",
               n, addr, (int)text_len(buf, (size_t)n), buf);

        /*
         * sendto(): odsyła datagram pod adres nadawcy. Datagram wysyła się w całości
         * albo wcale – nie ma częściowych zapisów, więc send_all() nie jest potrzebne.
         */
        if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, peer_len) < 0)
            perror("sendto");
    }

    printf("\n[UDP][srv] zamykam serwer\n");
    close(fd);
    return 0;
}
