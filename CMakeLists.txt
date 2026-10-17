# netstarter – starter pack aplikacji sieciowych klient/serwer (C, gniazda UNIX)
#

cmake_minimum_required(VERSION 3.20)
project(netstarter LANGUAGES C)

if(WIN32)
    message(WARNING "netstarter to kod linuksowy – zbuduj go na Linuxie (WSL / kontener).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -pedantic)

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

# UDP (3.0 / 3.5)
add_executable(udp_server udp/udp_server.c)
target_link_libraries(udp_server PRIVATE netcommon)

add_executable(udp_client udp/udp_client.c)
target_link_libraries(udp_client PRIVATE netcommon)

# Gniazda surowe (4.0)
add_executable(raw_server raw/raw_server.c)
target_link_libraries(raw_server PRIVATE netcommon)

add_executable(raw_client raw/raw_client.c)
target_link_libraries(raw_client PRIVATE netcommon)
