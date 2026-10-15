#!/usr/bin/env bash
# setcap.sh – nadaje binarkom capabilities, żeby działały jako zwykły użytkownik (etap 4.5).
#
# Użycie:  sudo scripts/setcap.sh
#
# Uruchamiać PO KAŻDYM buildzie: kompilator tworzy plik binarki od nowa, a capabilities
# są zapisane w atrybucie rozszerzonym pliku (xattr security.capability) – nowy plik ich nie ma.
#
#   cap_net_raw            – tworzenie gniazd surowych (raw_server, raw_client)
#   cap_net_bind_service   – bind() na port < 1024 (tcp_server, udp_server)
#   +ep                    – dodaj do zbiorów Effective (aktywne od startu) i Permitted (dozwolone)
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="build"

if [ "$(id -u)" -ne 0 ]; then
    echo "Ten skrypt wymaga roota: sudo $0" >&2
    exit 1
fi

# Nadaje capability jednej binarce; brakująca binarka = błąd (najpierw scripts/build.sh).
give_cap() {
    local cap="$1"
    local bin="$BUILD_DIR/$2"
    if [ ! -f "$bin" ]; then
        echo "Brak pliku $bin – najpierw zbuduj projekt: bash scripts/build.sh" >&2
        exit 1
    fi
    setcap "$cap" "$bin"
    echo "  $cap -> $bin"
}

echo "Nadaję capabilities:"
give_cap cap_net_raw+ep          raw_server
give_cap cap_net_raw+ep          raw_client
give_cap cap_net_bind_service+ep tcp_server
give_cap cap_net_bind_service+ep udp_server

echo
echo "Aktualny stan (getcap):"
for bin in tcp_server tcp_client udp_server udp_client raw_server raw_client; do
    if [ -f "$BUILD_DIR/$bin" ]; then
        caps="$(getcap "$BUILD_DIR/$bin")"
        echo "  ${caps:-$BUILD_DIR/$bin (brak capabilities)}"
    fi
done
