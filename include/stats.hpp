#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

class Stats {
 public:
  void on_start();
  void inc_active();
  void dec_active();
  void inc_requests();
  std::string render(int threads, size_t keys) const;

 private:
  std::chrono::steady_clock::time_point start_;
  std::atomic<int> active_{0};
  std::atomic<uint64_t> total_requests_{0};
};
