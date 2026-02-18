#include <cstdlib>
#include <iostream>
#include <string>

#include "server.hpp"

static uint16_t parse_u16(const char* s, uint16_t def) {
  try {
    int v = std::stoi(s);
    if (v < 1 || v > 65535) return def;
    return static_cast<uint16_t>(v);
  } catch (...) {
    return def;
  }
}

static int parse_i32(const char* s, int def, int lo, int hi) {
  try {
    int v = std::stoi(s);
    if (v < lo || v > hi) return def;
    return v;
  } catch (...) {
    return def;
  }
}

int main(int argc, char** argv) {
  uint16_t port = 8080;
  int threads = 8;
  int max_conns = 2000;
  size_t queue_cap = 4096;

  // Support: ./server 8080 (simple)
  if (argc >= 2 && std::string(argv[1]).rfind("--", 0) != 0) {
    port = parse_u16(argv[1], port);
  }

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char* flag) {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << "\n";
        std::exit(1);
      }
      return argv[++i];
    };

    if (a == "--port")
      port = parse_u16(need("--port"), port);
    else if (a == "--threads")
      threads = parse_i32(need("--threads"), threads, 1, 256);
    else if (a == "--max-conns")
      max_conns = parse_i32(need("--max-conns"), max_conns, 1, 2000000);
    else if (a == "--queue-cap")
      queue_cap =
          (size_t)parse_i32(need("--queue-cap"), (int)queue_cap, 1, 2000000);
    else if (a == "--help") {
      std::cout << "Usage: server [--port N] [--threads N] [--max-conns N] "
                   "[--queue-cap N]\n"
                << "Protocol: SET key value | GET key | DEL key | STATS | PING "
                   "| QUIT\n";
      return 0;
    }
  }

  Server s(port, threads, max_conns, queue_cap);
  return s.start() ? 0 : 1;
}
