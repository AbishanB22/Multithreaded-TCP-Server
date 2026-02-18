# Multithreaded TCP Key-Value Server (C++)

## Overview

This project is a high-performance multithreaded TCP key-value store implemented in modern C++. The server accepts concurrent client connections over TCP, processes requests using a fixed-size thread pool, and stores key-value data in a thread-safe in-memory hashmap.

The system is designed using core systems programming principles including socket programming, concurrency, synchronization, and graceful shutdown. It demonstrates how production servers handle multiple clients efficiently while maintaining performance and stability.

This project was built to demonstrate systems programming and backend infrastructure skills including:

- TCP server implementation

- Multithreading and synchronization

- Concurrent data structure design

- High-performance request handling

- Graceful shutdown and resource management

- Performance benchmarking and optimization

---

## Tech Stack

- Language: C++17
- Concurrency: std::thread, thread pool, std::shared_mutex, std::atomic
- Networking: POSIX sockets (TCP/IP)
- Build System: CMake
- Platform: Linux
- Benchmarking: Custom load testing client

---

## Key Features

- Multithreaded TCP server supporting concurrent client connections
- Fixed-size thread pool with bounded blocking queue
- Thread-safe key-value store using std::shared_mutex
- Custom TCP text protocol supporting GET, SET, DEL, STATS, PING
- Graceful shutdown using SIGINT signal handling
- Connection limit enforcement and request tracking
- Load testing client to benchmark throughput and concurrency

---

## Performance

The server was benchmarked using the included load testing client under concurrent load.

Benchmark command:

```bash
./build/bench_client --host 127.0.0.1 --port 8080 --clients 100 --seconds 10

--- 

## Result
clients=100 seconds=10 ops=6734786 ops/sec=673404

