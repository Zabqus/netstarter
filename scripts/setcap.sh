#!/usr/bin/env bash
# setcap.sh – nadaje binarkom capabilities, żeby działały bez roota

set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="build"

if [ "$(id -u)" -ne 0 ]; then
    echo "Ten skrypt wymaga roota: sudo $0" >&2
    exit 1
fi

# nadaje capability jednej binarce
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
