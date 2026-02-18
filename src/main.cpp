#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// -------------------- BlockingQueue (bounded) --------------------
template <typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(size_t capacity) : capacity_(capacity) {}

  // returns false if closed
  bool push(T item) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_not_full_.wait(lk, [&] { return closed_ || q_.size() < capacity_; });
    if (closed_) return false;
    q_.push_back(std::move(item));
    cv_not_empty_.notify_one();
    return true;
  }

  // returns nullopt if closed and empty
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_not_empty_.wait(lk, [&] { return closed_ || !q_.empty(); });
    if (q_.empty()) return std::nullopt;
    T item = std::move(q_.front());
    q_.pop_front();
    cv_not_full_.notify_one();
    return item;
  }

  void close() {
    std::lock_guard<std::mutex> lk(mu_);
    closed_ = true;
    cv_not_empty_.notify_all();
    cv_not_full_.notify_all();
  }

 private:
  size_t capacity_;
  std::mutex mu_;
  std::condition_variable cv_not_empty_;
  std::condition_variable cv_not_full_;
  std::deque<T> q_;
  bool closed_ = false;
};

// -------------------- KVStore (thread-safe) --------------------
class KVStore {
 public:
  bool set(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    map_[key] = value;
    return true;
  }

  std::optional<std::string> get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    return it->second;
  }

  bool del(const std::string& key) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    return map_.erase(key) > 0;
  }

  size_t size() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return map_.size();
  }

 private:
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::string> map_;
};

// -------------------- TCP helpers --------------------
static bool send_all(int fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = ::send(fd, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

static bool send_str(int fd, const std::string& s) {
  return send_all(fd, s.data(), s.size());
}

// Buffered line reader (handles TCP partial reads)
class LineReader {
 public:
  explicit LineReader(size_t max_line = 8192) : max_line_(max_line) {}

  // returns: line without '\n' (and strips optional '\r'), or nullopt on
  // disconnect/error.
  std::optional<std::string> read_line(int fd) {
    while (true) {
      // Check if we already have a full line buffered.
      auto pos = buffer_.find('\n');
      if (pos != std::string::npos) {
        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);

        // Strip CR if client uses \r\n
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.size() > max_line_) return std::string("**LINE_TOO_LONG**");
        return line;
      }

      // Otherwise, recv more.
      char tmp[4096];
      ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
      if (n < 0) {
        if (errno == EINTR) continue;
        return std::nullopt;
      }
      if (n == 0) return std::nullopt;  // disconnect
      buffer_.append(tmp, tmp + n);

      if (buffer_.size() > max_line_ + 4096) {
        // prevent unlimited growth if client never sends newline
        return std::string("**LINE_TOO_LONG**");
      }
    }
  }

 private:
  size_t max_line_;
  std::string buffer_;
};

// -------------------- Server --------------------
class Server {
 public:
  Server(uint16_t port, int threads, int max_conns, size_t queue_cap)
      : port_(port), threads_(threads), max_conns_(max_conns), q_(queue_cap) {}

  bool start() {
    // Create listen socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      perror("socket");
      return false;
    }

    int yes = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) <
        0) {
      perror("setsockopt");
      ::close(listen_fd_);
      return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("bind");
      ::close(listen_fd_);
      return false;
    }

    if (listen(listen_fd_, 256) < 0) {
      perror("listen");
      ::close(listen_fd_);
      return false;
    }

    start_time_ = std::chrono::steady_clock::now();
    running_.store(true);

    // Start worker threads
    for (int i = 0; i < threads_; i++) {
      workers_.emplace_back([this, i] { worker_loop(i); });
    }

    std::cerr << "Listening on port " << port_ << " with " << threads_
              << " threads\n";
    accept_loop();  // blocks

    // Shutdown path
    q_.close();
    for (auto& t : workers_) {
      if (t.joinable()) t.join();
    }

    if (listen_fd_ >= 0) ::close(listen_fd_);
    return true;
  }

  void stop() {
    running_.store(false);
    if (listen_fd_ >= 0) {
      // Closing the listen socket will usually break accept()
      ::shutdown(listen_fd_, SHUT_RDWR);
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    q_.close();
  }

 private:
  void accept_loop() {
    while (running_.load()) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int client_fd =
          ::accept(listen_fd_, (sockaddr*)&client_addr, &client_len);

      if (client_fd < 0) {
        if (!running_.load()) break;
        if (errno == EINTR) continue;
        perror("accept");
        continue;
      }

      int curr = active_connections_.load();
      if (curr >= max_conns_) {
        // Too many connections -> reject
        send_str(client_fd, "ERR server busy\n");
        ::close(client_fd);
        continue;
      }

      active_connections_.fetch_add(1);

      // Push into bounded queue (backpressure)
      if (!q_.push(client_fd)) {
        send_str(client_fd, "ERR server shutting down\n");
        ::close(client_fd);
        active_connections_.fetch_sub(1);
        break;
      }
    }
  }

  void worker_loop(int worker_id) {
    (void)worker_id;
    while (true) {
      auto item = q_.pop();
      if (!item.has_value()) break;  // closed + empty
      int fd = *item;
      serve_client(fd);
      ::close(fd);
      active_connections_.fetch_sub(1);
    }
  }

  void serve_client(int fd) {
    LineReader lr(8192);

    // Simple banner (optional)
    send_str(fd, "OK tcp-kv ready\n");

    while (running_.load()) {
      auto line_opt = lr.read_line(fd);
      if (!line_opt.has_value()) return;

      std::string line = *line_opt;
      if (line == "**LINE_TOO_LONG**") {
        send_str(fd, "ERR line too long\n");
        return;
      }

      if (line.empty()) continue;

      total_requests_.fetch_add(1);

      std::string resp = handle_command(line);
      if (!send_str(fd, resp)) return;
    }
  }

  std::string handle_command(const std::string& line) {
    // Tokenize: CMD key value...
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    // normalize cmd to uppercase-ish (basic)
    for (auto& c : cmd)
      c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));

    if (cmd == "GET") {
      std::string key;
      if (!(iss >> key)) return "ERR usage: GET key\n";
      auto v = kv_.get(key);
      if (!v) return "NOTFOUND\n";
      return "VALUE " + *v + "\n";
    }

    if (cmd == "SET") {
      std::string key;
      if (!(iss >> key)) return "ERR usage: SET key value\n";
      // remainder of the line after key is the value (preserve spaces)
      std::string value;
      std::getline(iss, value);
      if (!value.empty() && value.front() == ' ') value.erase(0, 1);
      kv_.set(key, value);
      return "OK\n";
    }

    if (cmd == "DEL") {
      std::string key;
      if (!(iss >> key)) return "ERR usage: DEL key\n";
      bool removed = kv_.del(key);
      return removed ? "OK\n" : "NOTFOUND\n";
    }

    if (cmd == "STATS") {
      auto now = std::chrono::steady_clock::now();
      auto up =
          std::chrono::duration_cast<std::chrono::seconds>(now - start_time_)
              .count();
      std::ostringstream out;
      out << "UPTIME " << up << "s\n";
      out << "ACTIVE_CONNECTIONS " << active_connections_.load() << "\n";
      out << "TOTAL_REQUESTS " << total_requests_.load() << "\n";
      out << "KEYS " << kv_.size() << "\n";
      out << "THREADS " << threads_ << "\n";
      return out.str();
    }

    if (cmd == "PING") return "PONG\n";
    if (cmd == "QUIT") return "OK bye\n";  // client can close after seeing this

    return "ERR unknown command\n";
  }

 private:
  uint16_t port_;
  int threads_;
  int max_conns_;
  BlockingQueue<int> q_;

  int listen_fd_ = -1;
  std::vector<std::thread> workers_;
  KVStore kv_;

  std::atomic<bool> running_{false};
  std::atomic<int> active_connections_{0};
  std::atomic<uint64_t> total_requests_{0};
  std::chrono::steady_clock::time_point start_time_;
};

// -------------------- CLI parsing --------------------
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
      queue_cap = static_cast<size_t>(
          parse_i32(need("--queue-cap"), (int)queue_cap, 1, 2000000));
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
