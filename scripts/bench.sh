#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../tcp-kv-server"
cmake -S . -B build
cmake --build build -j
./build/bench_client --host 127.0.0.1 --port 8080 --clients 100 --seconds 10
