cmake_minimum_required(VERSION 3.16)
project(tcp_kv_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell compiler where headers are
include_directories(${CMAKE_SOURCE_DIR}/../include)

# Server executable
add_executable(server
    ${CMAKE_SOURCE_DIR}/../src/main.cpp
    ${CMAKE_SOURCE_DIR}/../src/server.cpp
    ${CMAKE_SOURCE_DIR}/../src/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/../src/kvstore.cpp
    ${CMAKE_SOURCE_DIR}/../src/protocol.cpp
    ${CMAKE_SOURCE_DIR}/../src/stats.cpp
)

# Benchmark client
add_executable(bench_client
    ${CMAKE_SOURCE_DIR}/../client/bench_client.cpp
)

target_compile_options(server PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_compile_options(bench_client PRIVATE -O2 -Wall -Wextra -Wpedantic)
