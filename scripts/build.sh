#!/usr/bin/env bash
# build.sh – konfiguruje i buduje projekt do katalogu ./build
#
# Użycie:  scripts/build.sh            (Debug – domyślnie)
#          scripts/build.sh Release
#
# Uwaga: po każdym buildzie capabilities z binarek znikają (plik jest nadpisywany),
# więc dla etapu 4.5 trzeba ponownie uruchomić: sudo scripts/setcap.sh
set -euo pipefail

# Przechodzimy do katalogu głównego repo niezależnie od tego, skąd uruchomiono skrypt.
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-Debug}"

cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j"$(nproc)"

echo
echo "Gotowe. Binarki w katalogu: $(pwd)/build"
