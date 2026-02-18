#include "protocol.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>

LineReader::LineReader(size_t max_line) : max_line_(max_line) {}

std::optional<std::string> LineReader::read_line(int fd) {
  while (true) {
    auto pos = buffer_.find('\n');

    if (pos != std::string::npos) {
      std::string line = buffer_.substr(0, pos);

      buffer_.erase(0, pos + 1);

      if (!line.empty() && line.back() == '\r') line.pop_back();

      return line;
    }

    char tmp[4096];

    ssize_t n = recv(fd, tmp, sizeof(tmp), 0);

    if (n <= 0) return std::nullopt;

    buffer_.append(tmp, n);
  }
}

bool send_all(int fd, const char* data, size_t len) {
  size_t sent = 0;

  while (sent < len) {
    ssize_t n = send(fd, data + sent, len - sent, 0);

    if (n <= 0) return false;

    sent += n;
  }

  return true;
}

bool send_str(int fd, const std::string& s) {
  return send_all(fd, s.c_str(), s.size());
}
