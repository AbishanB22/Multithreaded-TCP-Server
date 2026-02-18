#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static bool send_all(int fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += (size_t)n;
  }
  return true;
}

static bool recv_line(int fd, std::string& out) {
  out.clear();
  char c;
  while (true) {
    ssize_t n = recv(fd, &c, 1, 0);
    if (n <= 0) return false;
    if (c == '\n') return true;
    out.push_back(c);
    if (out.size() > 8192) return false;
  }
}

static int connect_to(const std::string& host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }

  if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 8080;
  int clients = 50;
  int seconds = 5;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&]() { return std::string(argv[++i]); };
    if (a == "--host")
      host = need();
    else if (a == "--port")
      port = std::stoi(need());
    else if (a == "--clients")
      clients = std::stoi(need());
    else if (a == "--seconds")
      seconds = std::stoi(need());
    else if (a == "--help") {
      std::cout << "bench_client --host 127.0.0.1 --port 8080 --clients 100 "
                   "--seconds 10\n";
      return 0;
    }
  }

  std::atomic<bool> start{false};
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> ops{0};

  auto worker = [&](int id) {
    int fd = connect_to(host, port);
    if (fd < 0) return;

    // read banner
    std::string line;
    recv_line(fd, line);

    while (!start.load()) std::this_thread::yield();

    // simple workload: alternating SET/GET on per-thread key
    std::string key = "k" + std::to_string(id);
    std::string set_cmd = "SET " + key + " 123\n";
    std::string get_cmd = "GET " + key + "\n";

    while (!stop.load()) {
      if (!send_all(fd, set_cmd.c_str(), set_cmd.size())) break;
      if (!recv_line(fd, line)) break;

      if (!send_all(fd, get_cmd.c_str(), get_cmd.size())) break;
      if (!recv_line(fd, line)) break;

      ops.fetch_add(2);
    }

    close(fd);
  };

  std::vector<std::thread> ts;
  ts.reserve(clients);
  for (int i = 0; i < clients; i++) ts.emplace_back(worker, i);

  auto t0 = std::chrono::steady_clock::now();
  start.store(true);
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  stop.store(true);

  for (auto& t : ts) t.join();
  auto t1 = std::chrono::steady_clock::now();

  double sec = std::chrono::duration<double>(t1 - t0).count();
  uint64_t total = ops.load();
  std::cout << "clients=" << clients << " seconds=" << sec << " ops=" << total
            << " ops/sec=" << (total / sec) << "\n";
}
