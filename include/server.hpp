#pragma once
#include <cstddef>
#include <cstdint>

class Server {
 public:
  Server(uint16_t port, int threads, int max_conns, size_t queue_cap);
  bool start();  // blocking accept loop
  void stop();   // best-effort shutdown

 private:
  uint16_t port_;
  int threads_;
  int max_conns_;
  size_t queue_cap_;
};
