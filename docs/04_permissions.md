# 4.5 – Uprawnienia: gniazda surowe i porty < 1024 bez roota

## Problem

Dwie operacje są w Linuxie zastrzeżone dla uprzywilejowanych procesów:

| Operacja | Gdzie jądro to sprawdza | Wymagana capability |
|---|---|---|
| `socket(..., SOCK_RAW, ...)` | przy tworzeniu gniazda | `CAP_NET_RAW` |
| `bind()` na port < 1024 | przy `bind()` | `CAP_NET_BIND_SERVICE` |

Bez nich zwykły użytkownik dostaje:

```
$ ./build/tcp_server -p 80
bind: Permission denied
$ ./build/raw_server
brak uprawnień do gniazd surowych – uruchom z sudo lub nadaj cap_net_raw (scripts/setcap.sh)
```

**Dlaczego porty < 1024 są uprzywilejowane?** Historycznie (Unix, lata 80.) port < 1024
oznaczał, że po drugiej stronie działa usługa uruchomiona przez administratora, a nie
przez dowolnego użytkownika. Zwykły użytkownik nie może więc podszyć się pod SSH (22),
HTTP (80) czy DNS (53) na wspólnej maszynie.

**Dlaczego gniazda surowe są uprzywilejowane?** Pozwalają wysłać dowolny pakiet
(w tym podrobione nagłówki, np. cudzy adres źródłowy) i podsłuchiwać pakiety całego protokołu.

## Capabilities zamiast roota

Od Linuksa 2.2 uprawnienia roota są podzielone na ~40 osobnych **capabilities**
(`man 7 capabilities`). Proces może mieć tylko te, których potrzebuje, zamiast
„wszystko albo nic” jak przy UID 0.

Każdy proces ma kilka zbiorów capabilities (widoczne w `/proc/<PID>/status`):

- **Permitted (CapPrm)** – co proces w ogóle może mieć,
- **Effective (CapEff)** – co jest aktywne teraz (to sprawdza jądro),
- **Inheritable (CapInh)** – co może przejść przez `execve()`.

**File capabilities** to capabilities zapisane w atrybucie rozszerzonym pliku binarki
(`security.capability`). Gdy zwykły użytkownik uruchamia taki plik, proces dostaje je
przy `execve()`, mimo że UID nadal jest zwykły.

```
setcap cap_net_raw+ep build/raw_server
```

`+ep` = dodaj do zbioru **E**ffective i **P**ermitted (aktywne od razu po starcie).

## `scripts/setcap.sh`

Nadaje:

| Binarka | Capability |
|---|---|
| `raw_server`, `raw_client` | `cap_net_raw+ep` |
| `tcp_server`, `udp_server` | `cap_net_bind_service+ep` |

Na końcu wypisuje `getcap` dla wszystkich binarek. Klienci TCP/UDP nie potrzebują niczego –
łączą się na porty serwera, a sami dostają port efemeryczny (≥ 32768).

**Ważne:** każdy build tworzy plik binarki od nowa i capabilities znikają.
Po `scripts/build.sh` trzeba ponownie uruchomić `sudo scripts/setcap.sh`.

## Scenariusz demo

Na `net-server` jako `student`:

```
# 0. Upewnij się, że porty < 1024 są faktycznie zastrzeżone (domyślnie 1024).
#    Jeśli wyjdzie 0 – demo nie pokaże błędu (niektóre kontenery tak to ustawiają).
sysctl net.ipv4.ip_unprivileged_port_start

# 1. Bez uprawnień – błąd
bash scripts/build.sh
./build/tcp_server -p 80          # bind: Permission denied
./build/raw_server                # brak uprawnień do gniazd surowych ...
getcap build/tcp_server           # (pusto)

# 2. Nadanie capabilities
sudo scripts/setcap.sh

# 3. Jako student – działa
./build/tcp_server -p 80          # [TCP][srv] nasłuchuję na 0.0.0.0:80
./build/udp_server -p 53
./build/raw_server

# 4. Weryfikacja: proces NIE jest rootem
ps -o user,pid,cmd -C tcp_server  # USER = student
ss -tlnp | grep ':80'
```

Klient (z `net-client`, też jako `student`):

```
sudo scripts/setcap.sh            # raz po buildzie, dla raw_client
./build/tcp_client -h 192.168.1.201 -p 80 -m "port 80 bez roota"
./build/raw_client -h 192.168.1.201 -m "raw bez roota"
```

## Rozszerzenie: zasada najmniejszych uprawnień (zrzucanie capabilities)

Uprawnienia są potrzebne **tylko w jednym momencie**: przy `socket()` (raw) albo
`bind()` (port < 1024). Później gotowe gniazdo działa bez nich – jądro nie sprawdza
uprawnień ponownie przy `recv`/`send`. Dlatego serwery zaraz po przygotowaniu gniazda
wywołują `drop_capabilities()` (`common/net_common.c`):

```
tcp_server:  socket → bind → listen → capset(0) → accept ...
udp_server:  socket → bind → capset(0) → recvfrom ...
raw_server:  socket → capset(0) → recvfrom ...
```

`drop_capabilities()` woła wywołanie systemowe `capset` bezpośrednio przez `syscall()`
(glibc nie ma do niego funkcji, a libcap jest zakazany) i zeruje zbiory effective,
permitted i inheritable. Po wyzerowaniu permitted procesu nie da się już ich odzyskać.

Efekt: gdyby w kodzie obsługi klienta był błąd (np. przepełnienie bufora), napastnik
przejmuje proces bez żadnych uprawnień. W logu serwera:

```
[TCP][srv] capabilities zrzucone (gniazdo działa dalej)
[TCP][srv] nasłuchuję na 0.0.0.0:80 (Ctrl+C kończy)
```

Weryfikacja – porównaj zbiory pliku i działającego procesu:

```
getcap build/tcp_server                          # plik:   cap_net_bind_service=ep
grep Cap /proc/$(pgrep -n tcp_server)/status     # proces: CapPrm i CapEff = 0000000000000000
capsh --decode=0000000000000400                  # 0x400 = bit 10 = cap_net_bind_service
```

Uwaga: działa to także pod `sudo` – proces root po `capset(0)` ma nadal UID 0,
ale bez capabilities nie może np. czytać cudzych plików z pominięciem praw dostępu.

Możliwe dalsze kroki (nie zaimplementowane): `prctl(PR_CAPBSET_DROP)` – usunięcie
capabilities ze zbioru *bounding* (nie da się ich zdobyć nawet przez `execve` pliku
z capabilities), `prctl(PR_SET_NO_NEW_PRIVS)`, zmiana UID przez `setuid()` po starcie jako root.

## Porównanie alternatyw

| | `setcap` (na plik) | `sysctl net.ipv4.ip_unprivileged_port_start` | uruchamianie jako root |
|---|---|---|---|
| Zasięg | jedna binarka | cały system (namespace sieciowy) – każdy proces każdego użytkownika | jeden proces |
| Uprawnienia | tylko wybrana capability | tylko porty, nic więcej | wszystkie (pełny root) |
| Gniazda surowe | tak (`cap_net_raw`) | **nie** – dotyczy tylko portów | tak |
| Trwałość | do przebudowania pliku | do restartu (lub na stałe w `/etc/sysctl.d/`) | – |
| Bezpieczeństwo | dobre – minimum uprawnień | słabe – każdy może zająć np. port 22 lub 80 | najgorsze – błąd w programie = przejęty root |
| Kiedy używać | usługi wymagające jednej konkretnej operacji | kontenery / maszyny jednego użytkownika | nigdy dla programów sieciowych na stałe |

Przykład ustawienia sysctl (tylko do porównania – **skrypty projektu tego nie zmieniają**):

```
sudo sysctl net.ipv4.ip_unprivileged_port_start=80   # od teraz każdy może bindować porty ≥ 80
sudo sysctl net.ipv4.ip_unprivileged_port_start=1024 # przywrócenie domyślnej wartości
```

Inne spotykane rozwiązania: `systemd` (`AmbientCapabilities=CAP_NET_BIND_SERVICE`
w pliku usługi), przekierowanie portu w firewallu (np. 80 → 8080), `authbind`.

## Pytania kontrolne (skrót)

- **Czemu po rekompilacji serwer znowu dostaje `Permission denied`?** – capabilities są
  zapisane w xattr pliku, a build tworzy nowy plik.
- **Czy proces z `cap_net_bind_service` jest rootem?** – nie, UID się nie zmienia;
  ma tylko jedno konkretne uprawnienie.
- **Czemu po zrzuceniu capabilities serwer nadal działa?** – uprawnienia są sprawdzane
  tylko przy `socket()`/`bind()`; gotowe gniazdo (deskryptor) zostaje.
- **Czemu `sysctl` nie wystarczy dla `raw_server`?** – `ip_unprivileged_port_start`
  dotyczy tylko portów TCP/UDP; gniazda surowe zawsze wymagają `CAP_NET_RAW`.
