#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../tcp-kv-server"
cmake -S . -B build
cmake --build build -j
./build/server --port 8080 --threads 8
