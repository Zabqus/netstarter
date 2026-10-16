# 3.0 – Klient i serwer TCP oraz UDP (IPv4)

## Co jest realizowane

Dwie pary programów echo: klient wysyła tekst, serwer odsyła go bez zmian i loguje nadawcę.

| Program | Argumenty | Domyślnie |
|---|---|---|
| `tcp_server` | `[-4\|-6] [-p <port>]` | IPv4, port 5000 |
| `tcp_client` | `[-4\|-6] -h <host> [-p <port>] [-m "tekst"]` | dowolna wersja IP, port 5000 |
| `udp_server` | `[-4\|-6] [-p <port>]` | IPv4, port 5001 |
| `udp_client` | `[-4\|-6] -h <host> [-p <port>] [-m "tekst"]` | dowolna wersja IP, port 5001 |

Klient bez `-m` czyta linie ze stdin (Ctrl+D kończy), z `-m` wysyła jedną wiadomość i kończy.
Serwery kończy Ctrl+C (SIGINT) – gniazda są wtedy zamykane.
Opcja `-6` opisana jest w [02_ipv6.md](02_ipv6.md).

Adresy obsługuje wyłącznie `getaddrinfo()` / `getnameinfo()`, a struktury adresów to
`struct sockaddr_storage` – ten sam kod działa dla IPv4 i IPv6.

## TCP – sekwencja wywołań

```
      tcp_server                                   tcp_client
      ──────────                                   ──────────
      getaddrinfo(NULL, "5000", AI_PASSIVE)
      socket(AF_INET, SOCK_STREAM)
      setsockopt(SO_REUSEADDR)
      bind(0.0.0.0:5000)
      listen(backlog = 8)
      capset(0)  ← zrzucenie uprawnień (4.5)
  ┌─► accept()  ...czeka...                        getaddrinfo("192.168.1.201", "5000")
  │                                                socket(AF_INET, SOCK_STREAM)
  │       ◄──────── SYN / SYN-ACK / ACK ─────────  connect()
  │   accept() zwraca NOWE gniazdo cfd
  │   ┌─► recv(cfd)  ◄────────── dane ───────────  send_all()
  │   │   send_all(cfd) ──────── echo ──────────►  recv() aż przyjdzie tyle bajtów, ile wysłano
  │   └── (pętla)
  │       recv(cfd) == 0  ◄──────── FIN ─────────  close()
  │   close(cfd)
  └── (następny klient)
```

| Wywołanie | Co robi |
|---|---|
| `socket()` | tworzy gniazdo – sam deskryptor, jeszcze bez adresu |
| `setsockopt(SO_REUSEADDR)` | pozwala od razu zbindować port po restarcie mimo połączeń w TIME_WAIT |
| `bind()` | przypisuje gniazdu lokalny adres i port (`0.0.0.0` = wszystkie interfejsy) |
| `listen()` | przełącza gniazdo w tryb nasłuchu; jądro samo robi handshake i kolejkuje połączenia |
| `accept()` | zdejmuje połączenie z kolejki i zwraca **nowe** gniazdo do rozmowy z tym klientem |
| `connect()` | (klient) nawiązuje połączenie – handshake SYN, SYN-ACK, ACK |
| `recv()` | > 0 – liczba bajtów, 0 – druga strona zamknęła połączenie, -1 – błąd |
| `send_all()` | pętla `send()` do skutku – `send()` może przyjąć mniej bajtów, niż prosiliśmy |
| `close()` | zamyka połączenie (FIN) i zwalnia deskryptor |

Ważne szczegóły implementacji:

- Klienci obsługiwani są **sekwencyjnie** – drugi klient czeka w kolejce `listen()`,
  aż pierwszy się rozłączy.
- Klient **nie robi `bind()`** – jądro przy `connect()` dobiera adres źródłowy
  (wg routingu) i port efemeryczny.
- TCP to **strumień bajtów**: jeden `recv()` może zwrócić pół wiadomości albo dwie naraz.
  Klient zbiera odpowiedź w pętli (`recv_exact`), aż dostanie tyle bajtów, ile wysłał.
- `send()` używa `MSG_NOSIGNAL` – zapis do zamkniętego połączenia daje błąd `EPIPE`
  zamiast sygnału `SIGPIPE`, który zabiłby proces.
- Obsługa SIGINT jest **bez `SA_RESTART`** – blokujące `accept()`/`recv()` zwracają
  `EINTR`, pętla widzi flagę i serwer kończy się czysto.

## UDP – sekwencja wywołań

```
      udp_server                                   udp_client
      ──────────                                   ──────────
      getaddrinfo(NULL, "5001", AI_PASSIVE)        getaddrinfo("192.168.1.201", "5001")
      socket(AF_INET, SOCK_DGRAM)                  socket(AF_INET, SOCK_DGRAM)
      setsockopt(SO_REUSEADDR)                     setsockopt(SO_RCVTIMEO, 2 s)
      bind(0.0.0.0:5001)
      capset(0)
  ┌─► recvfrom(&peer) ◄──────── datagram ────────  sendto(serwer)
  │   sendto(peer) ───────────── datagram ───────► recvfrom()  (max 2 s, potem timeout)
  └── (pętla)                                      close()
```

- **Brak `listen`/`accept`** – UDP jest bezpołączeniowy. Jedno gniazdo obsługuje wszystkich
  klientów, adres nadawcy przychodzi z każdym datagramem (`recvfrom`), na niego idzie odpowiedź.
- **Granice wiadomości są zachowane**: jeden `sendto()` = jeden `recvfrom()`.
- **Datagram wysyła się w całości albo wcale** – `send_all()` nie jest potrzebne.
- **Brak potwierdzeń**: sukces `sendto()` znaczy tylko, że datagram wyszedł z maszyny.
  Gdy zginie (np. przez firewall), `recvfrom()` czekałby wiecznie – dlatego klient ma
  `SO_RCVTIMEO` i wypisuje `brak odpowiedzi (timeout 2 s)`, a z `-m` kończy się kodem 1.

## TCP vs UDP – porównanie

| | TCP | UDP |
|---|---|---|
| Typ gniazda | `SOCK_STREAM` | `SOCK_DGRAM` |
| Połączenie | tak (handshake, FIN) | nie |
| Serwer | `listen` + `accept` → osobne gniazdo na klienta | jedno gniazdo dla wszystkich |
| Dane | strumień bajtów, bez granic | osobne datagramy, granice zachowane |
| Niezawodność | potwierdzenia, retransmisje, kolejność | brak – datagram może zginąć lub przyjść 2 razy |
| Częściowy zapis | możliwy → `send_all()` | nie występuje |
| Wykrycie braku serwera | `connect: Connection refused` (RST) | dopiero timeout w kliencie |
| `ss` | stan `LISTEN` / `ESTAB` | stan `UNCONN` |

## Demo na dwóch kontenerach

Przygotowanie (oba kontenery, jako `student`):

```
cd ~/netstarter && git pull && bash scripts/build.sh
```

### TCP

```
net-server (t1):  ./build/tcp_server
net-server (t2):  sudo tcpdump -i eth0 -n tcp port 5000
net-client:       ./build/tcp_client -h 192.168.1.201 -m "test tcp"
```

Oczekiwany wynik:

```
net-client:  [TCP][cli] połączono z 192.168.1.201:5000
             [TCP][cli] odpowiedź (9 B): "test tcp"

net-server:  [TCP][srv] nasłuchuję na 0.0.0.0:5000 (Ctrl+C kończy)
             [TCP][srv] połączenie od 192.168.1.202:43122
             [TCP][srv] 9 B od 192.168.1.202:43122: "test tcp"
             [TCP][srv] 192.168.1.202:43122 rozłączył się
```

W `tcpdump`: `[S]`, `[S.]`, `[.]` (handshake), `[P.]` w obie strony (dane i echo), `[F.]` (zamknięcie).

Dalsze kroki:

```
net-server:  ss -tlnp | grep 5000                  # LISTEN 0.0.0.0:5000, proces tcp_server
net-client:  ./build/tcp_client -h 192.168.1.201   # tryb interaktywny, kilka linii, Ctrl+D
net-server:  ss -tnp | grep 5000                   # w trakcie: ESTAB z 192.168.1.202
```

**Obsługa sekwencyjna** – dwa klienci naraz:

```
net-client (t1):  ./build/tcp_client -h 192.168.1.201       # zostaw połączonego
net-client (t2):  ./build/tcp_client -h 192.168.1.201 -m "drugi"
```

Drugi klient łączy się (handshake robi jądro, połączenie czeka w kolejce `listen()`),
ale odpowiedź dostaje dopiero, gdy pierwszy zakończy Ctrl+D.

**Brak serwera** – zatrzymaj `tcp_server` (Ctrl+C):

```
net-client:  ./build/tcp_client -h 192.168.1.201 -m "x"
             # [TCP][cli] 192.168.1.201:5000: connect: Connection refused
```

W `tcpdump` widać `[S]` od klienta i `[R.]` (RST) od jądra serwera.

**`SO_REUSEADDR`** – stan TIME_WAIT dostaje ta strona, która zamyka połączenie **pierwsza**.
Żeby był to serwer:

```
net-client:  ./build/tcp_client -h 192.168.1.201    # połącz interaktywnie i zostaw
net-server:  Ctrl+C na tcp_server                   # serwer wysyła FIN pierwszy
net-client:  Ctrl+D                                 # klient zamyka swoją stronę
net-server:  ss -tan | grep 5000                    # TIME-WAIT 192.168.1.201:5000 192.168.1.202:...
net-server:  ./build/tcp_server                     # startuje od razu – dzięki SO_REUSEADDR
```

Bez `SO_REUSEADDR` `bind()` zwróciłby `Address already in use` przez ok. 60 s.

**`nc` zamiast naszego programu:**

```
net-client:  nc 192.168.1.201 5000                  # nc jako klient – nasz serwer odsyła wpisany tekst
net-server:  nc -l 5000                             # nc jako serwer (tcp_server wyłączony)
net-client:  ./build/tcp_client -h 192.168.1.201    # linie pojawiają się w nc; odpisz w nc, żeby klient dostał odpowiedź
```

### UDP

```
net-server (t1):  ./build/udp_server
net-server (t2):  sudo tcpdump -i eth0 -n udp port 5001
net-client:       ./build/udp_client -h 192.168.1.201 -m "test udp"
```

Oczekiwany wynik:

```
net-client:  [UDP][cli] serwer: 192.168.1.201:5001
             [UDP][cli] odpowiedź (9 B) od 192.168.1.201:5001: "test udp"

net-server:  [UDP][srv] nasłuchuję na 0.0.0.0:5001 (Ctrl+C kończy)
             [UDP][srv] 9 B od 192.168.1.202:5xxxx: "test udp"
```

W `tcpdump`: dokładnie dwa pakiety (żądanie i odpowiedź), bez handshake'u i zamykania.

Dalsze kroki:

```
net-server:  ss -ulnp | grep 5001                   # stan UNCONN – UDP nie ma LISTEN
net-client:  ./build/udp_client -h 192.168.1.201    # każda linia = osobny datagram
net-client:  nc -u 192.168.1.201 5001               # nc jako klient UDP
```

**Timeout** – zatrzymaj `udp_server` (Ctrl+C):

```
net-client:  ./build/udp_client -h 192.168.1.201 -m "nikt nie slucha"; echo "kod: $?"
             # [UDP][cli] brak odpowiedzi (timeout 2 s)
             # kod: 1
```

W `tcpdump` na serwerze widać datagram i odpowiedź jądra
`ICMP 192.168.1.201 udp port 5001 unreachable`. Klient jej nie raportuje, bo jego gniazdo
nie jest połączone (`connect`) – błędy ICMP trafiają tylko do połączonych gniazd UDP.
Dlatego w UDP jedyną informacją o braku serwera jest timeout.
