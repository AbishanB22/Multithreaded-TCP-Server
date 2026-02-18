#pragma once

#include <optional>
#include <string>

class LineReader {
 public:
  explicit LineReader(size_t max_line = 8192);

  // Returns a line without '\n' (and strips optional '\r').
  // Returns nullopt on disconnect/error.
  // If line too long, returns "**LINE_TOO_LONG**".
  std::optional<std::string> read_line(int fd);

 private:
  size_t max_line_;
  std::string buffer_;
};

bool send_all(int fd, const char* data, size_t len);
bool send_str(int fd, const std::string& s);

// Implemented in server.cpp (needs KV/Stats)
std::string handle_command(const std::string& line);
