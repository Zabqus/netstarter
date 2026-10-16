#!/usr/bin/env bash
# firewall.sh – zarządzanie regułami nftables projektu (etap 5.0).
#
# Użycie:  sudo scripts/firewall.sh load
#          sudo scripts/firewall.sh unload
#          sudo scripts/firewall.sh status
#
#   load     – ładuje reguły z firewall/netstarter.nft (liczniki od zera)
#   unload   – usuwa tabelę inet netstarter
#   status   – pokazuje reguły z licznikami
#
# Skrypt rusza tylko tabelę "inet netstarter" – reszta reguł systemu zostaje.
set -euo pipefail

cd "$(dirname "$0")/.."
FAMILY="inet"
TABLE="netstarter"
RULES="firewall/netstarter.nft"

usage() {
    echo "Użycie: sudo $0 load | unload | status" >&2
    exit 1
}

if [ "$(id -u)" -ne 0 ]; then
    echo "Ten skrypt wymaga roota: sudo $0 $*" >&2
    exit 1
fi

# czy tabela jest załadowana (nft list zwraca błąd, gdy jej nie ma)
table_loaded() {
    nft list table "$FAMILY" "$TABLE" >/dev/null 2>&1
}

case "${1:-}" in
    load)
        nft -f "$RULES"
        echo "Załadowano $RULES"
        echo
        nft list table "$FAMILY" "$TABLE"
        ;;
    unload)
        if table_loaded; then
            nft delete table "$FAMILY" "$TABLE"
            echo "Usunięto tabelę $FAMILY $TABLE – ruch netstarter nie jest filtrowany"
        else
            echo "Tabela $FAMILY $TABLE nie jest załadowana – nic do zrobienia"
        fi
        ;;
    status)
        if table_loaded; then
            nft list table "$FAMILY" "$TABLE"
        else
            echo "Tabela $FAMILY $TABLE nie jest załadowana"
        fi
        ;;
    *)
        usage
        ;;
esac
