#!/usr/bin/env bash
# firewall.sh – zarządzanie regułami nftables projektu (etap 5.0).
#
# Użycie:  sudo scripts/firewall.sh load [default|client|ipv6]
#          sudo scripts/firewall.sh unload
#          sudo scripts/firewall.sh status
#
#   load     – ładuje reguły z pliku (nft -f); ponowne load zeruje liczniki
#                default : firewall/netstarter.nft        (TCP accept, UDP i proto 253 drop)
#                client  : firewall/netstarter_client.nft (blokada tylko od net-client)
#                ipv6    : firewall/netstarter_ipv6.nft   (blokada tylko po IPv6)
#   unload   – usuwa całą tabelę inet netstarter (nft delete table)
#   status   – pokazuje reguły z licznikami (nft list table)
#
# Skrypt dotyka WYŁĄCZNIE tabeli "inet netstarter" – reszta reguł systemu zostaje bez zmian.
set -euo pipefail

cd "$(dirname "$0")/.."
FAMILY="inet"
TABLE="netstarter"

usage() {
    echo "Użycie: sudo $0 load [default|client|ipv6] | unload | status" >&2
    exit 1
}

if [ "$(id -u)" -ne 0 ]; then
    echo "Ten skrypt wymaga roota: sudo $0 $*" >&2
    exit 1
fi

# Czy tabela jest teraz załadowana? (nft list zwraca błąd, gdy jej nie ma)
table_loaded() {
    nft list table "$FAMILY" "$TABLE" >/dev/null 2>&1
}

case "${1:-}" in
    load)
        case "${2:-default}" in
            default) file="firewall/netstarter.nft" ;;
            client)  file="firewall/netstarter_client.nft" ;;
            ipv6)    file="firewall/netstarter_ipv6.nft" ;;
            *)       usage ;;
        esac
        nft -f "$file"
        echo "Załadowano $file"
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
