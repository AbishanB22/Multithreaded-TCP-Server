cmake_minimum_required(VERSION 3.16)
project(tcp_kv_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server
  ../src/main.cpp
  ../src/server.cpp
  ../src/thread_pool.cpp
  ../src/kvstore.cpp
  ../src/protocol.cpp
  ../src/stats.cpp
)

add_executable(bench_client
  ../client/bench_client.cpp
)

target_compile_options(server PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_compile_options(bench_client PRIVATE -O2 -Wall -Wextra -Wpedantic)
