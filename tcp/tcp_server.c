/*
 * tcp_server.c – serwer echo TCP.
 *
 * Uruchomienie:
 *   ./build/tcp_server [-4|-6] [-p <port>]    (domyślnie: IPv4, port 5000)
 *
 * Test bez klienta:  nc 127.0.0.1 5000   /   nc -6 ::1 5000
 *                    (wpisany tekst wraca z powrotem)
 *
 * Sekwencja wywołań:
 *   getaddrinfo → socket → setsockopt(SO_REUSEADDR) →
 *   [tylko IPv6: setsockopt(IPV6_V6ONLY)] → bind → listen →
 *   accept (pętla) → recv / send_all (pętla) → close(klient) → ... → close(serwer)
 *
 * Klienci obsługiwani są sekwencyjnie – jeden po drugim. Gdy jeden jest połączony,
 * następni czekają w kolejce jądra (backlog z listen()), aż obecny się rozłączy.
 * Ctrl+C (SIGINT) kończy serwer i zamyka gniazda.
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
#define BACKLOG      8      /* ile połączeń może czekać w kolejce na accept() */
#define BUF_SIZE     1024

/*
 * Jedyna zmienna globalna: flaga ustawiana przez obsługę SIGINT.
 * volatile sig_atomic_t – typ, który można bezpiecznie zapisać w handlerze sygnału.
 */
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

/* Tworzy gniazdo nasłuchujące na wszystkich adresach danej rodziny i zadanym porcie. */
static int create_listen_socket(int family, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;

    /*
     * getaddrinfo() wypełnia gotową strukturę adresu (sockaddr_in / sockaddr_in6),
     * dzięki czemu kod nie zależy od wersji IP.
     *   AI_PASSIVE + host NULL  → adres "dowolny" (0.0.0.0 / ::), czyli
     *                             nasłuch na wszystkich interfejsach,
     *   AI_NUMERICSERV          → port musi być liczbą (bez szukania w /etc/services).
     */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;          /* TCP */
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }
    /* Dla AI_PASSIVE i konkretnej rodziny wynik jest jeden – bierzemy pierwszy. */

    /* socket(): tworzy gniazdo – na razie to tylko deskryptor, bez adresu. */
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
        die("socket");

    /*
     * SO_REUSEADDR: pozwala od razu ponownie zbindować port po restarcie serwera.
     * Bez tego bind() zwraca "Address already in use", dopóki stare połączenia
     * wiszą w stanie TIME_WAIT (do ok. 60 s).
     */
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        die("setsockopt(SO_REUSEADDR)");

    /*
     * IPV6_V6ONLY = 1: gniazdo IPv6 przyjmuje WYŁĄCZNIE ruch IPv6.
     * Domyślnie w Linuxie (sysctl net.ipv6.bindv6only = 0) gniazdo na "::" przyjmowałoby
     * też połączenia IPv4, widziane jako adresy IPv4-mapped, np. ::ffff:192.168.1.202.
     * Z V6ONLY podział jest czysty: serwer -4 obsługuje IPv4, serwer -6 obsługuje IPv6
     * i oba mogą działać jednocześnie na tym samym porcie. Musi być ustawione przed bind().
     */
    if (res->ai_family == AF_INET6) {
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on) < 0)
            die("setsockopt(IPV6_V6ONLY)");
    }

    /* bind(): przypisuje gniazdu lokalny adres i port. Port < 1024 wymaga uprawnień. */
    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0)
        die("bind");

    freeaddrinfo(res);   /* struktura z getaddrinfo nie jest już potrzebna */

    /*
     * listen(): przełącza gniazdo w tryb nasłuchu – od teraz jądro samo przyjmuje
     * połączenia (three-way handshake) i odkłada je do kolejki o długości BACKLOG.
     */
    if (listen(fd, BACKLOG) < 0)
        die("listen");

    return fd;
}

/* Obsługa jednego klienta: odbiera dane i odsyła je z powrotem, aż klient się rozłączy. */
static void handle_client(int cfd, const char *peer)
{
    char buf[BUF_SIZE];

    while (!g_stop) {
        /*
         * recv(): czeka na dane od klienta. Zwraca:
         *   > 0  – liczbę odebranych bajtów,
         *   0    – klient zamknął połączenie (FIN), koniec rozmowy,
         *   -1   – błąd (errno).
         * TCP to strumień bajtów, nie wiadomości: jeden recv() może zwrócić
         * pół wiadomości albo dwie naraz. Dla echa to bez znaczenia – odsyłamy co przyszło.
         */
        ssize_t n = recv(cfd, buf, sizeof buf, 0);
        if (n < 0) {
            if (errno == EINTR)   /* przerwane sygnałem – pętla sprawdzi g_stop */
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

        /* Odsyłamy dokładnie te same bajty. send_all() dba o częściowe zapisy. */
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

    /* Proste ręczne parsowanie argumentów. */
    for (int i = 1; i < argc; i++) {
        int f = parse_family(argv[i]);   /* "-4" → AF_INET, "-6" → AF_INET6, inne → -1 */
        if (f != -1)
            family = f;
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = argv[++i];
        else
            usage(argv[0]);
    }

    /* Logi mają się pojawiać od razu, także gdy wyjście idzie do pliku / tee. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /*
     * Obsługa Ctrl+C. Celowo BEZ flagi SA_RESTART: wtedy blokujące accept()/recv()
     * po sygnale zwracają -1 z errno = EINTR, pętla widzi g_stop i kończy się czysto.
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        die("sigaction");

    int lfd = create_listen_socket(family, port);

    /* getsockname(): odczytuje adres, pod którym gniazdo faktycznie nasłuchuje. */
    struct sockaddr_storage local;
    socklen_t local_len = sizeof local;
    char addr[ADDR_STR_LEN];
    if (getsockname(lfd, (struct sockaddr *)&local, &local_len) < 0)
        die("getsockname");
    printf("[TCP][srv] nasłuchuję na %s (Ctrl+C kończy)\n",
           addr_to_str((struct sockaddr *)&local, local_len, addr, sizeof addr));

    while (!g_stop) {
        /*
         * accept(): czeka na połączenie z kolejki i zwraca NOWE gniazdo do rozmowy
         * z tym jednym klientem. Gniazdo lfd dalej tylko nasłuchuje.
         * sockaddr_storage pomieści adres dowolnej rodziny (IPv4 i IPv6).
         */
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

        close(cfd);   /* koniec rozmowy z tym klientem – wysyła FIN */
    }

    printf("\n[TCP][srv] zamykam serwer\n");
    close(lfd);
    return 0;
}
