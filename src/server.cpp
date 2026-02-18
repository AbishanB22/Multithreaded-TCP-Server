#include "server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "kvstore.hpp"
#include "protocol.hpp"
#include "stats.hpp"
#include "thread_pool.hpp"

// ---- Shared service state ----
static KVStore g_kv;
static Stats g_stats;

// Controls server lifetime
static std::atomic<bool> g_running{false};

// Used by stop() to break accept()
static std::atomic<int> g_listen_fd{-1};

// Thread count used in STATS output
static int g_threads = 0;

// Strict connection cap
static std::atomic<int> g_active_strict{0};

// ---- Command handler ----
std::string handle_command(const std::string& line) {
  std::istringstream iss(line);
  std::string cmd;
  iss >> cmd;

  for (auto& c : cmd)
    c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));

  if (cmd == "PING") return "PONG\n";

  if (cmd == "GET") {
    std::string key;
    if (!(iss >> key)) return "ERR usage: GET key\n";
    auto v = g_kv.get(key);
    if (!v) return "NOTFOUND\n";
    return "VALUE " + *v + "\n";
  }

  if (cmd == "SET") {
    std::string key;
    if (!(iss >> key)) return "ERR usage: SET key value\n";
    std::string value;
    std::getline(iss, value);
    if (!value.empty() && value.front() == ' ') value.erase(0, 1);
    g_kv.set(key, value);
    return "OK\n";
  }

  if (cmd == "DEL") {
    std::string key;
    if (!(iss >> key)) return "ERR usage: DEL key\n";
    bool removed = g_kv.del(key);
    return removed ? "OK\n" : "NOTFOUND\n";
  }

  if (cmd == "STATS") {
    return g_stats.render(g_threads, g_kv.size());
  }

  if (cmd == "QUIT") return "OK bye\n";

  return "ERR unknown command\n";
}

// ---- Per-connection serving ----
static void serve_client(int fd) {
  LineReader lr(8192);

  // banner
  send_str(fd, "OK tcp-kv ready\n");

  while (g_running.load()) {
    auto line_opt = lr.read_line(fd);
    if (!line_opt.has_value()) return;

    std::string line = *line_opt;

    if (line == "**LINE_TOO_LONG**") {
      send_str(fd, "ERR line too long\n");
      return;
    }
    if (line.empty()) continue;

    g_stats.inc_requests();

    std::string resp = handle_command(line);
    if (!send_str(fd, resp)) return;

    if (resp == "OK bye\n") return;
  }
}

// ---- Server ----
Server::Server(uint16_t port, int threads, int max_conns, size_t queue_cap)
    : port_(port),
      threads_(threads),
      max_conns_(max_conns),
      queue_cap_(queue_cap) {}

bool Server::start() {
  g_threads = threads_;
  g_stats.on_start();
  g_running.store(true);

  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return false;
  }

  g_listen_fd.store(listen_fd);

  int yes = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("setsockopt");
    ::close(listen_fd);
    g_listen_fd.store(-1);
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    ::close(listen_fd);
    g_listen_fd.store(-1);
    return false;
  }

  if (listen(listen_fd, 256) < 0) {
    perror("listen");
    ::close(listen_fd);
    g_listen_fd.store(-1);
    return false;
  }

  ThreadPool pool(threads_, queue_cap_);
  pool.start();

  std::cerr << "Listening on port " << port_ << " with " << threads_
            << " threads\n";
  std::cerr << "Press Ctrl+C to stop gracefully.\n";

  while (g_running.load()) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(listen_fd, (sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
      // If stop() closed the socket, accept will fail; exit loop cleanly
      if (!g_running.load()) break;
      if (errno == EINTR) continue;
      // EBADF / EINVAL happens if listen_fd got closed; treat as shutdown
      if (errno == EBADF || errno == EINVAL) break;
      perror("accept");
      continue;
    }

    // Active tracking + strict cap
    g_stats.inc_active();

    int now = g_active_strict.fetch_add(1) + 1;
    if (now > max_conns_) {
      send_str(client_fd, "ERR server busy\n");
      ::close(client_fd);
      g_stats.dec_active();
      g_active_strict.fetch_sub(1);
      continue;
    }

    bool ok = pool.submit([client_fd]() {
      serve_client(client_fd);
      ::close(client_fd);
      g_stats.dec_active();
      g_active_strict.fetch_sub(1);
    });

    if (!ok) {
      send_str(client_fd, "ERR server shutting down\n");
      ::close(client_fd);
      g_stats.dec_active();
      g_active_strict.fetch_sub(1);
      break;
    }
  }

  // Stop accepting new work and wait for worker threads to finish
  pool.stop();

  // Close listen socket if still open
  int fd = g_listen_fd.exchange(-1);
  if (fd != -1) {
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
  }

  std::cerr << "Server stopped.\n";
  return true;
}

void Server::stop() {
  // Flip running flag first so loops stop
  g_running.store(false);

  // Closing listen fd breaks accept() and exits accept loop
  int fd = g_listen_fd.exchange(-1);
  if (fd != -1) {
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
  }
}
