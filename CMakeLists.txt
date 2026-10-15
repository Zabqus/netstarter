# ---------------------------------------------------------------------------
# netstarter – starter pack aplikacji sieciowych klient/serwer (C, gniazda UNIX)
#
# Budowanie (na Linuxie):  scripts/build.sh
#   albo ręcznie:          cmake -S . -B build && cmake --build build
# Binarki lądują bezpośrednio w katalogu build/.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)
project(netstarter LANGUAGES C)

# Kod używa nagłówków UNIX (sys/socket.h, netinet/ip.h ...), więc na Windows
# (MinGW/MSVC) się nie zbuduje. W CLion trzeba wybrać toolchain WSL lub zdalny.
if(WIN32)
    message(WARNING "netstarter to kod linuksowy – zbuduj go na Linuxie (WSL / kontener).")
endif()

# Czysty C11: -std=c11 (a nie -std=gnu11), bez rozszerzeń GNU języka.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Domyślnie budujemy w trybie Debug (symbole dla gdb).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Wszystkie ostrzeżenia – kod ma się kompilować bez żadnego.
add_compile_options(-Wall -Wextra -pedantic)

# Przy -std=c11 glibc ukrywa funkcje spoza standardu C (getaddrinfo, struct iphdr,
# NI_MAXHOST ...). _DEFAULT_SOURCE włącza POSIX.1-2008 oraz rozszerzenia BSD/Linux.
add_compile_definitions(_DEFAULT_SOURCE)

# Binarki trafiają bezpośrednio do katalogu budowania (build/).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# Wspólna biblioteka statyczna z helperami (common/)
# ---------------------------------------------------------------------------
add_library(netcommon STATIC common/net_common.c)
target_include_directories(netcommon PUBLIC common)

# ---------------------------------------------------------------------------
# Programy – po jednym add_executable na program, każdy linkowany z netcommon.
# Dopisywane w kolejnych etapach.
# ---------------------------------------------------------------------------

# TCP (3.0 / 3.5)
add_executable(tcp_server tcp/tcp_server.c)
target_link_libraries(tcp_server PRIVATE netcommon)

add_executable(tcp_client tcp/tcp_client.c)
target_link_libraries(tcp_client PRIVATE netcommon)
