#include "protocol.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

// NOTE: handle_command() needs access to KV store + stats, so we’ll wire it in
// server.cpp. To keep protocol.cpp simple, we only keep networking primitives +
// LineReader here. We'll still declare handle_command in protocol.hpp but
// define it in server.cpp.

LineReader::LineReader(size_t max_line) : max_line_(max_line) {}

std::optional<std::string> LineReader::read_line(int fd) {
  while (true) {
    auto pos = buffer_.find('\n');
    if (pos != std::string::npos) {
      std::string line = buffer_.substr(0, pos);
      buffer_.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > max_line_) return std::string("**LINE_TOO_LONG**");
      return line;
    }

    char tmp[4096];
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return std::nullopt;
    }
    if (n == 0) return std::nullopt;
    buffer_.append(tmp, tmp + n);

    if (buffer_.size() > max_line_ + 4096) {
      return std::string("**LINE_TOO_LONG**");
    }
  }
}

bool send_all(int fd, const char* data, size_t len) {
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

bool send_str(int fd, const std::string& s) {
  return send_all(fd, s.data(), s.size());
}
