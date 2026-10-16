# 4.0 – Klient i serwer na gniazdach surowych (warstwa 3)

## Co jest realizowane

Własny protokół warstwy 3 **NS** („netstarter”), przenoszony bezpośrednio w IP
z numerem protokołu **253**. Bez TCP, UDP i portów – nagłówek, sumę kontrolną
i dopasowanie odpowiedzi robimy sami.

| Program | Argumenty | Domyślnie |
|---|---|---|
| `raw_server` | `[-4\|-6]` | IPv4 |
| `raw_client` | `[-4\|-6] -h <host> [-m "tekst"]` | IPv4 |

Klient bez `-m` wysyła każdą linię ze stdin jako osobne żądanie z kolejnym `seq`.
Oba programy wymagają `CAP_NET_RAW` – `sudo` albo `sudo scripts/setcap.sh`
(patrz [04_permissions.md](04_permissions.md)). Bez uprawnień:

```
brak uprawnień do gniazd surowych – uruchom z sudo lub nadaj cap_net_raw (scripts/setcap.sh)
```

Opcja `--hdrincl` (ręczne budowanie nagłówka IPv4) jest zaplanowana jako etap 7 (bonus)
i nie jest jeszcze zaimplementowana.

## Dlaczego protokół 253

Pole `protocol` (IPv4) / `next header` (IPv6) mówi, co jest za nagłówkiem IP
(6 = TCP, 17 = UDP, 1 = ICMP ...). Numery **253 i 254** są zarezerwowane do eksperymentów
(RFC 3692). Jądro nie ma dla nich obsługi, więc pakiet trafia **tylko** do gniazd surowych
otwartych z tym numerem protokołu.

## Protokół NS (`common/raw_proto.h`)

```
 0               1               2               3
+---------------+---------------+---------------+---------------+
|          magic = "NS"         |    version    |     type      |
+---------------+---------------+---------------+---------------+
|              id               |              seq              |
+---------------+---------------+---------------+---------------+
|            length             |           checksum            |
+---------------+---------------+---------------+---------------+
|                      dane (length bajtów) ...                 |
```

| Pole | Rozmiar | Znaczenie |
|---|---|---|
| `magic` | 2 B | `0x4E53` („NS”) – odróżnia nasze pakiety od przypadkowych |
| `version` | 1 B | `1` |
| `type` | 1 B | `1` = REQ (żądanie), `2` = REP (odpowiedź) |
| `id` | 2 B | identyfikator klienta: `getpid() & 0xFFFF` (jak w `ping`) |
| `seq` | 2 B | numer kolejny żądania |
| `length` | 2 B | długość danych za nagłówkiem |
| `checksum` | 2 B | suma kontrolna RFC 1071 po nagłówku + danych |

Wszystkie pola wielobajtowe są w **network byte order** (`htons`/`ntohs`).
Struktura ma `__attribute__((packed))` (bez bajtów wyrównania), a `_Static_assert`
sprawdza przy kompilacji, że ma dokładnie 12 bajtów.

Funkcje wspólne dla klienta i serwera:

| Funkcja | Co robi |
|---|---|
| `ns_build()` | wypełnia nagłówek, kopiuje dane, liczy sumę z `checksum = 0` i ją wpisuje |
| `ns_validate()` | sprawdza długość, magic, wersję, `length` i sumę kontrolną |
| `ns_locate()` | znajduje początek pakietu NS w odebranym buforze (różnica IPv4/IPv6) |
| `ns_open_socket()` | `socket(family, SOCK_RAW, 253)` + czytelny komunikat przy braku uprawnień |

### Suma kontrolna RFC 1071 (`inet_checksum` w `common/net_common.c`)

1. Dane traktujemy jako ciąg słów 16-bitowych (big-endian) i sumujemy je w zmiennej 32-bitowej.
2. Nieparzysta długość – ostatni bajt uzupełniamy zerem.
3. Przeniesienia powyżej 16 bitów dodajemy z powrotem do młodszych 16 bitów
   (arytmetyka uzupełnień do jedności).
4. Wynik to negacja bitowa (`~`).

Nadawca liczy sumę z polem `checksum = 0` i wpisuje wynik. Odbiorca liczy sumę po **całym**
pakiecie razem z polem `checksum` – jeśli nic się nie zmieniło, wychodzi **0**.
Tej samej sumy używają nagłówek IPv4, ICMP, TCP i UDP.

## Sekwencja wywołań

```
      raw_server                                     raw_client
      ──────────                                     ──────────
      socket(AF_INET, SOCK_RAW, 253)                 socket(AF_INET, SOCK_RAW, 253)
      capset(0)  ← zrzucenie uprawnień (4.5)         getaddrinfo(host, NULL, SOCK_RAW, 253)
                                                     setsockopt(SO_RCVTIMEO, 2 s)
                                                     ns_build(REQ, id, seq, dane)
  ┌─► recvfrom(&peer) ◄──── [IP | NS REQ] ─────────  sendto(serwer)
  │   ns_locate → ns_validate → type == REQ?
  │   ns_build(REP, to samo id/seq/dane)
  │   sendto(peer) ──────── [IP | NS REP] ─────────► recvfrom() w pętli, aż REP z naszym id i seq
  └── (pętla)                                        wypisanie RTT, close()
```

- **Brak `bind`/`listen`/`accept` i portów** – gniazdo surowe dostaje **każdy** pakiet IP
  z protokołem 253, który dotrze do maszyny (na dowolny jej adres).
- **Wysyłanie**: przekazujemy do `sendto()` tylko pakiet NS. Jądro dokleja nagłówek IP
  (adres źródłowy wg routingu, TTL/hop limit, protocol = 253, w IPv4 sumę kontrolną IP).
- **Kopie pakietów**: każde gniazdo surowe protokołu 253 dostaje kopię każdego pasującego
  pakietu. Serwer ignoruje więc pakiety REP (np. własne odpowiedzi przy kliencie na tej samej
  maszynie), a klient czeka tylko na REP ze swoim `id` i `seq` – resztę pomija, pilnując
  łącznego czasu oczekiwania (SO_RCVTIMEO liczy się od nowa przy każdym `recvfrom()`).
- **RTT** mierzony zegarem `CLOCK_MONOTONIC` od `sendto()` do odebrania pasującego REP.

## IPv4 vs IPv6 – co zwraca `recvfrom()`

Najważniejsza różnica (i częste pytanie kontrolne), obsłużona w `ns_locate()`:

```
IPv4:  buf → | nagłówek IPv4 (ihl*4 B, zwykle 20) | nagłówek NS (12 B) | dane |
IPv6:  buf → | nagłówek NS (12 B) | dane |
```

| | IPv4 (`AF_INET`) | IPv6 (`AF_INET6`) |
|---|---|---|
| Nagłówek IP w buforze | **tak** – dane NS od `buf + ip->ihl * 4` | **nie** – dane NS od `buf[0]` |
| Długość nagłówka | zmienna (opcje IP), `ihl` w słowach 32-bit | stała 40 B + nagłówki rozszerzeń – i tak zdejmowane |
| Pola nagłówka | dostępne wprost (`struct iphdr`): src, dst, TTL, protocol | tylko adres nadawcy z `recvfrom()`; hop limit / dst przez `recvmsg()` + `IPV6_RECVHOPLIMIT` / `IPV6_RECVPKTINFO` |
| Źródło zachowania | tradycja BSD | RFC 3542 (API gniazd surowych IPv6) |

Dlatego serwer po IPv4 wypisuje pola nagłówka IP, a po IPv6 tylko informację, że nagłówek
zdjęło jądro. Klient po IPv4 wypisuje dodatkowo TTL odpowiedzi.

### Co się dzieje, gdy nikt nie słucha

Jeśli na serwerze **żadne** gniazdo surowe nie ma otwartego protokołu 253, jądro nie wie,
co zrobić z pakietem i odsyła błąd:

- IPv4: ICMP *destination unreachable – protocol 253 unreachable* (typ 3, kod 2),
- IPv6: ICMPv6 *parameter problem – unrecognized next header* (typ 4, kod 1).

Klient tego nie raportuje (czeka na pakiet NS) – wypisuje `brak odpowiedzi (timeout 2 s)`.

## Demo na dwóch kontenerach

Przygotowanie (oba kontenery):

```
cd ~/netstarter && git pull && bash scripts/build.sh && sudo scripts/setcap.sh
```

Bez `setcap.sh` każde polecenie poniżej trzeba poprzedzić `sudo`.

### IPv4

```
net-server (t1):  ./build/raw_server
net-server (t2):  sudo tcpdump -i eth0 -n -X 'ip proto 253'
net-client:       ./build/raw_client -h 192.168.1.201 -m "hej"
```

Oczekiwany wynik:

```
net-client:  [RAW][cli] serwer: 192.168.1.201, protokół 253, id=4242
             [RAW][cli] REQ id=4242 seq=1, 3 B danych
             [RAW][cli] REP od 192.168.1.201 id=4242 seq=1, 3 B: "hej"  RTT=0.2xx ms
             [RAW][cli]   IPv4: ttl=64

net-server:  [RAW][srv] capabilities zrzucone (gniazdo działa dalej)
             [RAW][srv] czekam na pakiety IPv4 protokołu 253 (Ctrl+C kończy)
             [RAW][srv] REQ od 192.168.1.202 id=4242 seq=1, 3 B: "hej"
             [RAW][srv]   IPv4: src=192.168.1.202 dst=192.168.1.201 ttl=64 proto=253 hlen=20 B tot_len=35 B
             [RAW][srv]   odesłano REP (15 B)
```

`tot_len = 35 B` = 20 B nagłówka IP + 12 B nagłówka NS + 3 B danych.

W `tcpdump` (`ip-proto-253`) po 20 bajtach nagłówka IP widać nasz nagłówek:

```
4e53 0101 1092 0001 0003 xxxx 6865 6a
│    │ │  │    │    │    │    └ "hej"
│    │ │  │    │    │    └ checksum
│    │ │  │    │    └ length = 3
│    │ │  │    └ seq = 1
│    │ │  └ id (np. 0x1092 = 4242)
│    │ └ type = 1 (REQ); w odpowiedzi 02 (REP)
│    └ version = 1
└ magic "NS"
```

Tryb interaktywny – `seq` rośnie z każdą linią:

```
net-client:  ./build/raw_client -h 192.168.1.201
```

### IPv6

```
net-server (t1):  ./build/raw_server -6
net-server (t2):  sudo tcpdump -i eth0 -n -X 'ip6 proto 253'
net-client:       ./build/raw_client -6 -h fd00:10::201 -m "hej6"
```

Na serwerze:

```
[RAW][srv] REQ od fd00:10::202 id=4242 seq=1, 4 B: "hej6"
[RAW][srv]   IPv6: nagłówek zdjęty przez jądro, dane NS od bajtu 0 (16 B w buforze)
[RAW][srv]   odesłano REP (16 B)
```

Porównaj: po IPv4 `recvfrom()` zwróciło 35 B (z nagłówkiem IP), po IPv6 – 16 B
(sam pakiet NS: 12 + 4).

`raw_server -4` i `raw_server -6` mogą działać jednocześnie (dwa osobne gniazda).

### Brak serwera

Zatrzymaj `raw_server` (Ctrl+C), zostaw `tcpdump` z filtrem rozszerzonym o ICMP:

```
net-server:  sudo tcpdump -i eth0 -n 'ip proto 253 or icmp or ip6 proto 253 or icmp6'
net-client:  ./build/raw_client -h 192.168.1.201 -m "x"; echo "kod: $?"      # timeout, kod: 1
net-client:  ./build/raw_client -6 -h fd00:10::201 -m "x"                    # timeout
```

W `tcpdump`: `ICMP 192.168.1.201 protocol 253 unreachable` oraz
`ICMP6, parameter problem, next header`.

### Bez uprawnień

```
net-client:  bash scripts/build.sh            # nowy plik – capabilities znikają
net-client:  ./build/raw_client -h 192.168.1.201 -m "x"
             # brak uprawnień do gniazd surowych – uruchom z sudo lub nadaj cap_net_raw (scripts/setcap.sh)
net-client:  sudo scripts/setcap.sh
```
