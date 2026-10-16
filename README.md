# netstarter

Klient i serwer echo w C11 na gniazdach UNIX (bez bibliotek sieciowych).
Klient wysyła tekst, serwer odsyła go z powrotem.

## Zakres

- 3.0 – TCP i UDP po IPv4 (`tcp/`, `udp/`)
- 3.5 – TCP i UDP po IPv6 (flaga `-6`)
- 4.0 – gniazda surowe, własny protokół IP nr 253 (`raw/`, `common/raw_proto.h`)
- 4.5 – gniazda surowe i porty < 1024 bez roota (`scripts/setcap.sh`)
- 5.0 – firewall nftables (`firewall/netstarter.nft`, `scripts/firewall.sh`)

## Budowanie (Linux)

```
bash scripts/build.sh
sudo scripts/setcap.sh
```

## Uruchomienie

```
./build/tcp_server [-4|-6] [-p port]          ./build/tcp_client -h host [-p port] [-m "tekst"]
./build/udp_server [-4|-6] [-p port]          ./build/udp_client -h host [-p port] [-m "tekst"]
./build/raw_server [-4|-6]                    ./build/raw_client [-4|-6] -h host [-m "tekst"]

sudo scripts/firewall.sh load | status | unload
```

Domyślne porty: TCP 5000, UDP 5001.
