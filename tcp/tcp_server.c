/*
 * tcp_server.c – serwer echo TCP.
 */
#include <errno.h>       /* errno, EINTR */
#include <netdb.h>       /* getaddrinfo, freeaddrinfo, gai_strerror */
#include <signal.h>      /* sigaction, sig_atomic_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset, strcmp */
#include <unistd.h>      /* close */
#include <netinet/in.h>  /* IPPROTO_IPV6, IPV6_V6ONLY */
#include <sys/socket.h>  /* socket, setsockopt, bind, listen, accept, recv */

#include "net_common.h"

#define DEFAULT_PORT "5000"
#define BACKLOG      8      /* ile połączeń może czekać w kolejce */
#define BUF_SIZE     1024

// Jedyna zmienna globalna – flaga ustawiana po Ctrl+C.
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Użycie: %s [-4|-6] [-p <port>]\n"
                    "  -4         IPv4 (domyślnie)\n"
                    "  -6         IPv6\n"
                    "  -p <port>  port nasłuchu (domyślnie " DEFAULT_PORT ")\n", prog);
    exit(EXIT_FAILURE);
}

// Tworzy gniazdo nasłuchujące na wszystkich adresach i zadanym porcie.
static int create_listen_socket(int family, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;

    // przygotowujemy adres serwera: dowolny adres (0.0.0.0 / ::) i nasz port
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;                                                /* TCP */
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;                                   /* adres "dowolny", port liczbowy */

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    // tworzymy gniazdo TCP
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
        die("socket");

    // pozwalamy od razu ponownie użyć portu po restarcie (bez czekania na TIME_WAIT)
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        die("setsockopt(SO_REUSEADDR)");

    // IPv6: gniazdo przyjmuje tylko IPv6
    if (res->ai_family == AF_INET6) {
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on) < 0)
            die("setsockopt(IPV6_V6ONLY)");
    }

    // przypisujemy gniazdu adres i port (port < 1024)
    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0)
        die("bind");

    freeaddrinfo(res);

    // zaczynamy nasłuchiwać – jądro samo przyjmuje połączenia i kolejkuje
    if (listen(fd, BACKLOG) < 0)
        die("listen");

    return fd;
}

// Rozmowa z jednym klientem: odbieramy dane i odsyłamy je z powrotem.
static void handle_client(int cfd, const char *peer)
{
    char buf[BUF_SIZE];

    while (!g_stop) {
        // odbieramy dane: > 0 – bajty, 0 – klient się rozłączył, -1 – błąd
        ssize_t n = recv(cfd, buf, sizeof buf, 0);
        if (n < 0) {
            if (errno == EINTR)   /* przerwane przez Ctrl+C */
                continue;
            perror("recv");
            return;
        }
        if (n == 0) {
            printf("[TCP][srv] %s rozłączył się\n", peer);
            return;
        }

        printf("[TCP][srv] %zd B od %s: \"%.*s\"\n",
               n, peer, (int)text_len(buf, (size_t)n), buf);

        // odsyłamy te same bajty (echo)
        if (send_all(cfd, buf, (size_t)n) < 0) {
            perror("send");
            return;
        }
    }
}

int main(int argc, char *argv[])
{
    int family = AF_INET;
    const char *port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        int f = parse_family(argv[i]);
        if (f != -1)
            family = f;
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = argv[++i];
        else
            usage(argv[0]);
    }

    // logi wypisujemy od razu, linia po linii
    setvbuf(stdout, NULL, _IOLBF, 0);

    // Ctrl+C: bez SA_RESTART, żeby accept()/recv() przerwały się i serwer się zamknął
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        die("sigaction");

    int lfd = create_listen_socket(family, port);

    // odczytujemy adres, na którym nasłuchujemy (do logu)
    struct sockaddr_storage local;
    socklen_t local_len = sizeof local;
    char addr[ADDR_STR_LEN];
    if (getsockname(lfd, (struct sockaddr *)&local, &local_len) < 0)
        die("getsockname");
    printf("[TCP][srv] nasłuchuję na %s (Ctrl+C kończy)\n",
           addr_to_str((struct sockaddr *)&local, local_len, addr, sizeof addr));

    while (!g_stop) {
        // akceptujemy połączenie –  nowe gniazdo
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof peer;
        int cfd = accept(lfd, (struct sockaddr *)&peer, &peer_len);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        addr_to_str((struct sockaddr *)&peer, peer_len, addr, sizeof addr);
        printf("[TCP][srv] połączenie od %s\n", addr);

        handle_client(cfd, addr);

        // zamykamy połączenie z klientem (FIN)
        close(cfd);
    }

    printf("\n[TCP][srv] zamykam serwer\n");
    close(lfd);
    return 0;
}
