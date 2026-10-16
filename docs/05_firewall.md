# 5.0 – Firewall: przepuszczanie i blokowanie ruchu (nftables)

## Co jest realizowane

Na serwerze (`net-server`) ładujemy reguły nftables, które:

| Ruch | Program | Reguła | Efekt u klienta |
|---|---|---|---|
| TCP 5000 | `tcp_server` | `accept` | działa normalnie |
| UDP 5001 | `udp_server` | `drop` | `brak odpowiedzi (timeout 2 s)` |
| IP proto 253 (IPv4) | `raw_server -4` | `drop` | `brak odpowiedzi (timeout 2 s)` |
| IP proto 253 (IPv6) | `raw_server -6` | `drop` | `brak odpowiedzi (timeout 2 s)` |

Każda reguła ma `counter`, więc widać, ile pakietów w nią trafiło.

Pliki:

| Plik | Opis |
|---|---|
| `firewall/netstarter.nft` | reguły podstawowe (tabela powyżej) |
| `firewall/netstarter_client.nft` | wariant: blokada tylko z adresu klienta `192.168.1.202` / `fd00:10::202` |
| `firewall/netstarter_ipv6.nft` | wariant: blokada tylko po IPv6 (TCP przez `reject`, reszta przez `drop`) |
| `scripts/firewall.sh` | `load [default\|client\|ipv6]`, `unload`, `status` |

## Jak to działa

### Własna tabela

```
table inet netstarter {
    chain input {
        type filter hook input priority filter; policy accept;
        tcp dport 5000 counter accept
        udp dport 5001 counter drop
        ip protocol 253 counter drop
        meta nfproto ipv6 meta l4proto 253 counter drop
    }
}
```

- **Tabela `inet netstarter`** – rodzina `inet` obejmuje IPv4 i IPv6 jedną tabelą.
  Nie dotykamy domyślnej tabeli systemu (`inet filter`) – `unload` usuwa tylko naszą tabelę
  i nic więcej się nie zmienia.
- **Łańcuch bazowy `input`** – `type filter hook input` podpina go pod punkt zaczepienia
  (hook) netfiltra w jądrze.
- **`policy accept`** – wszystko, czego nie dopasuje żadna reguła (SSH, ICMP, ruch systemu),
  przechodzi. Blokujemy tylko ruch netstarter.
- **`counter`** – liczba pakietów i bajtów dopasowanych do reguły.
- **`ip protocol 253`** dopasowuje tylko IPv4. Dla IPv6 używamy **`meta l4proto 253`** –
  to protokół warstwy 4 ustalony *po* przejściu przez nagłówki rozszerzeń IPv6
  (pole `nexthdr` pierwszego nagłówka mogłoby wskazywać np. na nagłówek fragmentacji).

### Hook `input` – gdzie w drodze pakietu działa filtr

```
karta sieciowa ─► [tcpdump widzi tu] ─► prerouting ─► routing ─┬─► input ─► gniazdo procesu
                                                              │   (nasze reguły)
                                                              └─► forward ─► postrouting ─► dalej
```

- `input` widzi pakiety **adresowane do tej maszyny**, po decyzji routingu,
  **przed dostarczeniem do gniazda** – także gniazda surowego. Dlatego `drop` dla
  protokołu 253 działa na `raw_server`.
- `tcpdump` przechwytuje pakiety **wcześniej**, zanim zadziała netfilter.
  Na demo: `tcpdump` na serwerze pokazuje, że datagram UDP **dotarł**, a serwer i tak go
  nie dostaje – został wyrzucony w `input`.

### `drop` vs `reject`

| | `drop` | `reject` |
|---|---|---|
| Co robi | po cichu wyrzuca pakiet | wyrzuca i odsyła informację o błędzie |
| TCP u klienta | `connect()` wisi, SYN jest ponawiany (do ok. 2 min) | od razu `Connection refused` (RST) |
| UDP / raw u klienta | timeout | timeout (ICMP unreachable nie jest raportowane do niepołączonego gniazda) |
| Dla atakującego | nie wiadomo, czy host w ogóle istnieje | widać, że host istnieje i filtruje |

Wariant `ipv6` pokazuje obie wersje: TCP po IPv6 jest odrzucane przez `reject with tcp reset`,
a UDP i protokół 253 przez `drop`.

### Kilka tabel na tym samym hooku

Jeśli w systemie jest też inna tabela z łańcuchem na `hook input` (np. `inet filter`),
pakiet przechodzi przez **wszystkie** łańcuchy po kolei (wg `priority`).
`accept` kończy tylko bieżący łańcuch – inny łańcuch dalej może pakiet wyrzucić.
`drop` jest ostateczny. Nasz `accept` dla TCP 5000 niczego więc nie „otwiera” w innych tabelach.

### Ładowanie bez dublowania reguł

Każdy plik `.nft` zaczyna się od:

```
table inet netstarter
delete table inet netstarter
table inet netstarter { ... }
```

Utwórz (jeśli nie ma) → usuń → zdefiniuj od nowa. `nft -f` wykonuje cały plik atomowo,
więc kolejne `load` nie dubluje reguł, a liczniki startują od zera.
Przełączanie między wariantami to po prostu `load` innego pliku.

## Demo na dwóch kontenerach

### Przygotowanie

```
# oba kontenery
cd ~/netstarter && git pull && bash scripts/build.sh && sudo scripts/setcap.sh

# net-server – sprawdzenie, że żadnych naszych reguł jeszcze nie ma
sudo scripts/firewall.sh status        # Tabela inet netstarter nie jest załadowana
sudo nft list tables                   # inne tabele systemu (zostaną nietknięte)
```

Na `net-server` uruchom serwery (każdy w osobnym terminalu albo w tle z `&`):

```
./build/tcp_server
./build/udp_server
./build/raw_server
./build/raw_server -6
```

`raw_server -4` i `raw_server -6` mogą działać jednocześnie – to osobne gniazda.

### Krok 1 – bez firewalla wszystko działa

Na `net-client`:

```
./build/tcp_client -h 192.168.1.201 -m "tcp"
./build/udp_client -h 192.168.1.201 -m "udp"
./build/raw_client -h 192.168.1.201 -m "raw4"
./build/raw_client -6 -h fd00:10::201 -m "raw6"
```

Każdy klient wypisuje odpowiedź serwera.

### Krok 2 – załadowanie reguł

Na `net-server`:

```
sudo scripts/firewall.sh load
```

Wypisze tabelę z licznikami równymi zero:

```
table inet netstarter {
	chain input {
		type filter hook input priority filter; policy accept;
		tcp dport 5000 counter packets 0 bytes 0 accept comment "netstarter: TCP 5000 accept"
		udp dport 5001 counter packets 0 bytes 0 drop comment "netstarter: UDP 5001 drop"
		...
```

### Krok 3 – TCP przechodzi, UDP i raw dostają timeout

Na `net-client` te same polecenia co w kroku 1:

```
./build/tcp_client -h 192.168.1.201 -m "tcp"          # odpowiedź – działa
./build/udp_client -h 192.168.1.201 -m "udp"          # [UDP][cli] brak odpowiedzi (timeout 2 s)
./build/raw_client -h 192.168.1.201 -m "raw4"         # [RAW][cli] brak odpowiedzi (timeout 2 s)
./build/raw_client -6 -h fd00:10::201 -m "raw6"       # [RAW][cli] brak odpowiedzi (timeout 2 s)
```

Log serwerów UDP i raw – **nic nowego** (pakiety do nich nie dotarły).

### Krok 4 – liczniki rosną, tcpdump widzi pakiety

Na `net-server`:

```
sudo scripts/firewall.sh status        # packets > 0 przy każdej regule
```

Przykład: przy TCP kilka pakietów (handshake + dane + FIN), przy UDP 1, przy proto 253 po 1.

Dowód, że pakiet doszedł do maszyny, ale został wyrzucony w `input`
(terminal 2 na serwerze, potem ponowne `udp_client` na kliencie):

```
sudo tcpdump -i eth0 -n 'udp port 5001 or ip proto 253 or ip6 proto 253'
```

`tcpdump` pokazuje przychodzące żądanie, a odpowiedzi brak – serwer go nigdy nie dostał.
Bez firewalla przy wyłączonym raw_server jądro odesłałoby ICMP „protocol unreachable” –
z regułą `drop` nie ma nawet tego.

### Krok 5 – usunięcie reguł

```
sudo scripts/firewall.sh unload
```

Na `net-client` powtórz krok 1 – wszystko znów działa.

## Warianty

### Blokada tylko z adresu klienta

```
net-server:  sudo scripts/firewall.sh load client
net-client:  ./build/udp_client -h 192.168.1.201 -m "z klienta"      # timeout
net-server:  ./build/udp_client -h 192.168.1.201 -m "z serwera"      # działa – źródło to 192.168.1.201, nie klient
net-server:  sudo scripts/firewall.sh status                          # rośnie tylko licznik reguły "klient v4"
```

To samo dla raw (`raw_client` z klienta – timeout, z serwera – działa) i po IPv6
(`-h fd00:10::201` – rośnie licznik „klient v6”).

### Blokada tylko po IPv6 (oraz drop vs reject)

```
net-server:  sudo scripts/firewall.sh load ipv6
             ./build/tcp_server -6        # obok działającego tcp_server (IPv4)
             ./build/udp_server -6        # obok działającego udp_server (IPv4)

net-client:  ./build/tcp_client -h 192.168.1.201 -m v4     # działa
             ./build/tcp_client -h fd00:10::201 -m v6      # od razu: connect: Connection refused (reject → RST)
             ./build/udp_client -h 192.168.1.201 -m v4     # działa
             ./build/udp_client -h fd00:10::201 -m v6      # timeout (drop)
             ./build/raw_client -h 192.168.1.201 -m v4     # działa
             ./build/raw_client -6 -h fd00:10::201 -m v6   # timeout (drop)
             ping -6 -c 2 fd00:10::201                     # działa – ICMPv6 nie jest blokowane
```

Różnica `reject` vs `drop` dla TCP: zamień w `firewall/netstarter_ipv6.nft`
`reject with tcp reset` na `drop`, `load ipv6` ponownie i spróbuj połączyć się po IPv6 –
`tcp_client` będzie wisiał na `connect()` (Ctrl+C), a `tcpdump` pokaże ponawiane SYN-y.

## Diagnostyka

```
sudo nft list ruleset                 # wszystkie reguły w systemie (nasze + inne tabele)
sudo nft list table inet netstarter   # tylko nasze, z licznikami
sudo nft -c -f firewall/netstarter.nft  # sprawdzenie składni bez ładowania
```

Jeśli ruch jest blokowany mimo `unload`, sprawdź `nft list ruleset` (inne tabele)
oraz czy firewall Proxmoxa jest wyłączony dla kontenerów (Datacenter / węzeł / CT → Firewall).
