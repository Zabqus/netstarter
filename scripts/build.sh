#!/usr/bin/env bash
# build.sh – konfiguruje i buduje projekt do katalogu ./build

set -euo pipefail

# Przechodzimy do katalogu głównego repo niezależnie od tego, skąd uruchomiono skrypt.
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-Debug}"

cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j"$(nproc)"

echo
echo "Gotowe. Binarki w katalogu: $(pwd)/build"
