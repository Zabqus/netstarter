# Pytania kontrolne – krótkie odpowiedzi

Odnośniki w nawiasach wskazują, gdzie w kodzie dany temat występuje.

## Podstawy gniazd

**1. Co zwraca `socket()` i czym jest gniazdo?**
Deskryptor pliku (liczba całkowita) – uchwyt do obiektu w jądrze, przez który proces
komunikuje się po sieci. Zaraz po `socket()` gniazdo nie ma adresu ani połączenia.
Trzy argumenty: rodzina (`AF_INET`/`AF_INET6`), typ (`SOCK_STREAM`/`SOCK_DGRAM`/`SOCK_RAW`),
protokół (0 = domyślny dla typu, albo np. 253 dla gniazda surowego).

**2. Czym jest network byte order i po co `htons()`?**
Protokoły sieciowe przesyłają liczby wielobajtowe w kolejności **big-endian** (starszy bajt
pierwszy). Procesory x86 są little-endian, więc przed wpisaniem liczby do nagłówka trzeba
ją zamienić: `htons()`/`htonl()` (host → network, 16/32 bity), a po odczytaniu
`ntohs()`/`ntohl()`. Na maszynie big-endian te funkcje nic nie robią – kod jest przenośny.
(`raw_proto.h`: wszystkie pola `ns_hdr`; porty i adresy w `sockaddr` wypełnia za nas `getaddrinfo`.)

**3. Po co `getaddrinfo()` zamiast ręcznego wypełniania `sockaddr_in`?**
Zamienia nazwę/adres i port na gotowe struktury adresów dowolnej rodziny (IPv4 i IPv6),
od razu w network byte order. Dzięki temu ten sam kod obsługuje obie wersje IP –
zmiana na IPv6 to tylko `ai_family`. Starsze `gethostbyname()` obsługuje tylko IPv4
i nie jest wielowątkowo bezpieczne. Odwrotną zamianę (adres → tekst) robi `getnameinfo()`.
Błędy obu funkcji to kody dla `gai_strerror()`, nie `errno`.

**4. Po co `struct sockaddr_storage`?**
To struktura na tyle duża i wyrównana, że pomieści adres każdej rodziny
(`sockaddr_in` 16 B, `sockaddr_in6` 28 B). Używamy jej, gdy z góry nie wiadomo, jaki adres
przyjdzie – np. w `accept()` i `recvfrom()`. Funkcje gniazd przyjmują wskaźnik na ogólny
`struct sockaddr *` i długość, a rodzinę rozpoznają po polu `sa_family`.

**5. Czym różni się gniazdo z `socket()` od gniazda z `accept()`?**
Gniazdo z `socket()` + `bind()` + `listen()` jest **nasłuchujące** – służy tylko do przyjmowania
nowych połączeń, nigdy nie przenosi danych. `accept()` zwraca **nowe** gniazdo połączone
z jednym konkretnym klientem (para adres:port po obu stronach) – przez nie idzie `recv`/`send`.
Po `close()` gniazda klienta nasłuchujące działa dalej. (`tcp_server.c`: `lfd` vs `cfd`.)

**6. Co robi `listen()` i czym jest backlog?**
Przełącza gniazdo w tryb nasłuchu. Od tej chwili jądro samo przeprowadza three-way handshake
z klientami i odkłada gotowe połączenia do kolejki; `accept()` tylko je z niej zdejmuje.
Backlog (u nas 8) to długość tej kolejki. Dlatego drugi klient przy obsłudze sekwencyjnej
łączy się od razu, ale odpowiedź dostaje dopiero, gdy pierwszy skończy.

**7. Czemu klient nie robi `bind()`?**
Nie musi mieć stałego portu – serwer i tak dowie się o adresie klienta z `accept()`/`recvfrom()`.
Jądro przy `connect()` (TCP) albo pierwszym `sendto()` (UDP) samo wybiera adres źródłowy
wg tablicy routingu i wolny port efemeryczny (`net.ipv4.ip_local_port_range`, domyślnie 32768–60999).
Serwer robi `bind()`, bo klienci muszą znać jego port.

**8. Co daje `SO_REUSEADDR` i co to TIME_WAIT?**
Po zamknięciu połączenia strona, która zamknęła je **pierwsza**, trzyma je w stanie TIME_WAIT
(2×MSL, w Linuksie 60 s), żeby spóźnione pakiety starego połączenia nie trafiły do nowego.
Bez `SO_REUSEADDR` restart serwera w tym czasie kończy się `bind: Address already in use`.
Z opcją `bind()` się udaje. Opcja nie pozwala dwóm procesom jednocześnie nasłuchiwać na tym
samym porcie (to robi `SO_REUSEPORT`).

## Przesyłanie danych

**9. `recv()` zwraca 0 vs -1 – czym się różnią?**
`> 0` – liczba odebranych bajtów. **`0`** – druga strona zamknęła połączenie (przyszedł FIN),
nie będzie więcej danych – normalny koniec rozmowy. **`-1`** – błąd, szczegóły w `errno`
(np. `ECONNRESET` – przyszedł RST, `EINTR` – przerwane sygnałem, `EAGAIN` – timeout przy
`SO_RCVTIMEO`). (`tcp_server.c`: `handle_client`.)

**10. Strumień TCP vs datagramy UDP – co to znaczy w praktyce?**
TCP przesyła **ciąg bajtów bez granic**: dwa `send()` mogą przyjść jednym `recv()`, a jeden
`send()` może być rozbity na kilka `recv()`. Aplikacja musi sama wiedzieć, gdzie kończy się
wiadomość (u nas klient czeka, aż dostanie tyle bajtów, ile wysłał – `recv_exact`).
UDP zachowuje granice: jeden `sendto()` = jeden datagram = jeden `recvfrom()`.
Za to UDP nie gwarantuje dostarczenia, kolejności ani braku duplikatów.

**11. Po co `send_all()`?**
`send()` zwraca liczbę bajtów przyjętych do bufora nadawczego jądra – może być mniejsza niż
żądana (częściowy zapis, np. gdy bufor jest prawie pełny). `send_all()` ponawia `send()`
dla reszty danych, aż wyśle wszystko. Dla UDP niepotrzebne – datagram idzie w całości albo wcale.

**12. Co to `SIGPIPE` i czemu `MSG_NOSIGNAL`?**
Zapis do gniazda, którego druga strona zamknęła połączenie, domyślnie wysyła procesowi
sygnał `SIGPIPE`, który go **zabija**. Z flagą `MSG_NOSIGNAL` dostajemy zamiast tego błąd
`EPIPE` i możemy go obsłużyć. (`net_common.c`: `send_all`.)

**13. Jak klient UDP rozpoznaje, że serwer nie działa?**
Tylko po timeoucie (`SO_RCVTIMEO`, 2 s). Jądro serwera odsyła ICMP *port unreachable*,
ale Linux raportuje takie błędy (jako `ECONNREFUSED`) tylko do **połączonych** gniazd UDP
(po `connect()`). Nasz klient używa `sendto()` bez `connect()`, więc widzi tylko brak odpowiedzi.
Ten sam timeout chroni przed zgubionym datagramem i blokadą przez firewall.

**14. Jak serwer kończy się czysto po Ctrl+C?**
Handler `SIGINT` ustawia flagę `volatile sig_atomic_t` (jedyna zmienna globalna).
Handler jest zarejestrowany przez `sigaction()` **bez `SA_RESTART`**, więc blokujące
`accept()`/`recv()`/`recvfrom()` przerywa się z `errno = EINTR`, pętla sprawdza flagę,
a program zamyka gniazda `close()` i kończy się.

## IPv6

**15. Co robi `IPV6_V6ONLY` i co to adresy `::ffff:`?**
Linux domyślnie (`net.ipv6.bindv6only = 0`) pozwala gniazdu IPv6 na `::` przyjmować także IPv4 –
klient IPv4 widoczny jest wtedy jako adres **IPv4-mapped** `::ffff:192.168.1.202`.
`IPV6_V6ONLY = 1` ogranicza gniazdo do czystego IPv6. Efekt: serwery `-4` i `-6` mogą działać
równocześnie na tym samym porcie, a w logach widać prawdziwą wersję IP. Ustawia się przed `bind()`.

**16. Co trzeba było zmienić, żeby programy działały po IPv6?**
Prawie nic: `ai_family = AF_INET6` w `getaddrinfo()` (flaga `-6`), `IPV6_V6ONLY` w serwerach,
a w klientach `AF_UNSPEC`. Reszta kodu jest wspólna, bo używa `getaddrinfo`/`getnameinfo`
i `sockaddr_storage` zamiast konkretnych `sockaddr_in`.

**17. Czemu adres IPv6 w logu jest w nawiasach `[...]`?**
Adres IPv6 sam zawiera dwukropki, więc `fd00:10::202:5001` byłby niejednoznaczny.
Konwencja z URL (RFC 3986): `[fd00:10::202]:5001`. (`net_common.c`: `addr_to_str`.)

## Gniazda surowe

**18. Czym jest gniazdo surowe i po co własny protokół 253?**
`SOCK_RAW` daje dostęp do warstwy 3 – omijamy TCP/UDP i sami budujemy wszystko, co jest za
nagłówkiem IP. Numer 253 jest zarezerwowany do eksperymentów (RFC 3692), jądro go nie obsługuje,
więc pakiety trafiają wyłącznie do naszych gniazd. Nie ma portów – gniazdo dostaje wszystkie
pakiety protokołu 253 docierające do maszyny.

**19. Nagłówek IP w gniazdach surowych IPv4 vs IPv6?**
IPv4: `recvfrom()` zwraca pakiet **z nagłówkiem IP** – dane zaczynają się od `buf + ip->ihl * 4`
(`ihl` to długość nagłówka w słowach 32-bit, zwykle 5 → 20 B; zmienna przez opcje IP).
IPv6: jądro **zdejmuje** nagłówek IPv6 – dane od `buf[0]` (RFC 3542); pola nagłówka
(hop limit, adres docelowy) można dostać tylko przez `recvmsg()` z danymi pomocniczymi.
Przy wysyłaniu w obu przypadkach nagłówek IP dokłada jądro. (`raw_proto.h`: `ns_locate`.)

**20. Jak działa suma kontrolna RFC 1071?**
Sumujemy dane jako słowa 16-bitowe w zmiennej 32-bitowej, przeniesienia powyżej 16 bitów
dodajemy z powrotem (dodawanie w uzupełnieniu do jedności), wynik negujemy (`~`).
Nadawca liczy z polem `checksum = 0` i wpisuje wynik; odbiorca liczy po całym pakiecie razem
z sumą – poprawny pakiet daje 0. Wykrywa pojedyncze błędy bitów, ale nie zamianę kolejności
słów 16-bitowych. Używają jej IPv4, ICMP, TCP, UDP. (`net_common.c`: `inet_checksum`.)

**21. Czemu klient raw filtruje po `type`, `id` i `seq`?**
Każde gniazdo surowe protokołu 253 dostaje **kopię** każdego pasującego pakietu.
Klient może więc zobaczyć własne REQ (test na jednej maszynie), odpowiedzi dla innych klientów
albo spóźnioną odpowiedź na poprzednie żądanie. Akceptuje tylko REP ze swoim `id` (PID)
i bieżącym `seq` – tak samo działa `ping` z ICMP Echo.

**22. Po co `__attribute__((packed))` w `struct ns_hdr`?**
Kompilator może wstawiać między pola struktury bajty wyrównania. Dla nagłówka sieciowego
układ w pamięci musi odpowiadać dokładnie bajtom na kablu, więc `packed` wyłącza wyrównanie.
`_Static_assert(sizeof(struct ns_hdr) == 12)` sprawdza to przy kompilacji.

**23. Co robi `IP_HDRINCL`?**
Opcja gniazda surowego IPv4 (`setsockopt(IPPROTO_IP, IP_HDRINCL, 1)`): od tej chwili
**aplikacja sama buduje cały nagłówek IPv4** (wersja, IHL, TTL, protokół, adres źródłowy
i docelowy) i podaje go w buforze `sendto()`. Jądro wtedy nie dokłada swojego nagłówka
(uzupełnia tylko m.in. sumę kontrolną IP i `tot_len`, a `id` – jeśli jest 0). Pozwala np.
ustawić dowolny TTL czy adres źródłowy. W IPv6 nie ma odpowiednika – nagłówek IPv6 zawsze
buduje jądro (pola ustawia się opcjami, np. `IPV6_UNICAST_HOPS`).
W projekcie zaplanowane jako opcja `--hdrincl` (etap 7, bonus).

**24. Co się dzieje z pakietem 253, gdy na serwerze nic nie słucha?**
Jądro nie zna protokołu i nie ma gniazda surowego, więc odsyła ICMP *protocol unreachable*
(IPv4) albo ICMPv6 *parameter problem – unrecognized next header* (IPv6). Klient widzi timeout.

## Uprawnienia

**25. Czemu porty < 1024 są uprzywilejowane?**
Historyczna konwencja Uniksa: port < 1024 gwarantował, że usługę uruchomił administrator,
a nie dowolny użytkownik – nikt nie podszyje się pod SSH (22), HTTP (80), DNS (53).
`bind()` na taki port wymaga `CAP_NET_BIND_SERVICE`. Granicę ustawia
`sysctl net.ipv4.ip_unprivileged_port_start` (domyślnie 1024).

**26. Capabilities vs root?**
Root (UID 0) ma wszystkie uprawnienia naraz. Capabilities dzielą je na ~40 osobnych
(`CAP_NET_RAW`, `CAP_NET_BIND_SERVICE`, `CAP_DAC_OVERRIDE`...). Proces może dostać tylko tę
jedną, której potrzebuje, i nadal działać jako zwykły użytkownik – błąd w programie nie daje
napastnikowi całego systemu. Zbiory procesu: permitted, effective, inheritable
(widoczne w `/proc/<PID>/status`).

**27. Jak działa `setcap cap_net_raw+ep` i czemu po buildzie trzeba go powtórzyć?**
Zapisuje capability w atrybucie rozszerzonym pliku (`security.capability`). Przy `execve()`
proces dostaje ją w zbiorach **e**ffective i **p**ermitted. Build tworzy nowy plik binarki
bez tego atrybutu – stąd `sudo scripts/setcap.sh` po każdym buildzie.

**28. Czemu serwer działa dalej po zrzuceniu capabilities?**
Jądro sprawdza uprawnienia tylko przy `socket(SOCK_RAW)` i `bind()` na port < 1024.
Gotowe gniazdo (deskryptor) zostaje i `recv`/`send` nie wymagają już uprawnień.
Serwery wołają `capset` z zerowymi zbiorami zaraz po przygotowaniu gniazda (zasada najmniejszych
uprawnień). (`net_common.c`: `drop_capabilities`.)

**29. `setcap` vs `sysctl ip_unprivileged_port_start` vs root?**
`setcap` – jedna binarka, jedna capability, najbezpieczniej. `sysctl` – cały system:
każdy proces każdego użytkownika może zająć niskie porty, i nie pomaga dla gniazd surowych.
Root – działa wszystko, ale błąd w programie = przejęty system. Szczegóły:
[04_permissions.md](04_permissions.md).

## Firewall (nftables)

**30. Jak działa hook `input` w nftables?**
Netfilter ma w jądrze punkty zaczepienia na drodze pakietu: `prerouting` → decyzja routingu →
`input` (pakiety **do tej maszyny**) albo `forward` (przekazywane dalej) → `output`/`postrouting`
(wychodzące). Łańcuch bazowy z `type filter hook input` widzi pakiety po routingu, a przed
dostarczeniem do gniazda procesu – także surowego. `drop` w nim sprawia, że pakiet nigdy nie
dotrze do serwera.

**31. Czemu `tcpdump` widzi pakiet, który firewall wyrzucił?**
`tcpdump` przechwytuje pakiety na interfejsie (gniazdo `AF_PACKET`), **zanim** trafią do
netfiltra. Widać więc żądanie przychodzące do maszyny, ale bez odpowiedzi – dowód, że pakiet
dotarł i został odrzucony w `input`.

**32. `drop` vs `reject`?**
`drop` po cichu wyrzuca pakiet – klient TCP ponawia SYN aż do timeoutu, UDP/raw czekają.
`reject` dodatkowo odsyła błąd (TCP RST albo ICMP unreachable) – TCP od razu dostaje
`Connection refused`. `drop` nie zdradza, że host istnieje; `reject` szybciej informuje
uprawnionych klientów. (`firewall/netstarter_ipv6.nft` pokazuje oba.)

**33. Czemu własna tabela `inet netstarter`, a nie reguły w `inet filter`?**
Nie mieszamy się z regułami systemu – `unload` to po prostu `nft delete table inet netstarter`
i nic innego się nie zmienia. Rodzina `inet` obsługuje IPv4 i IPv6 jedną tabelą.
Gdy kilka tabel ma łańcuch na tym samym hooku, pakiet przechodzi przez wszystkie
(wg `priority`): `accept` kończy tylko bieżący łańcuch, `drop` jest ostateczny.

**34. `ip protocol 253` vs `meta l4proto 253`?**
`ip protocol` to pole nagłówka IPv4 – dopasowuje tylko IPv4. W IPv6 pole `nexthdr` pierwszego
nagłówka może wskazywać nagłówek rozszerzenia, więc używamy `meta l4proto` – protokołu warstwy 4
ustalonego po przejściu przez wszystkie nagłówki rozszerzeń. `meta nfproto ipv6` zawęża regułę
do IPv6.

**35. Po co `counter` w regułach?**
Zlicza pakiety i bajty dopasowane do reguły. `nft list table inet netstarter` pokazuje, że ruch
faktycznie trafia w regułę – np. licznik UDP rośnie o 1 przy każdym `udp_client`, choć serwer
nic nie loguje.
