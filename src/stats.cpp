#include "stats.hpp"

#include <sstream>

void Stats::on_start() { start_ = std::chrono::steady_clock::now(); }

void Stats::inc_active() { active_.fetch_add(1); }

void Stats::dec_active() { active_.fetch_sub(1); }

void Stats::inc_requests() { total_requests_.fetch_add(1); }

std::string Stats::render(int threads, size_t keys) const {
  auto now = std::chrono::steady_clock::now();
  auto up =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_).count();

  std::ostringstream out;

  out << "UPTIME " << up << "s\n";
  out << "ACTIVE_CONNECTIONS " << active_.load() << "\n";
  out << "TOTAL_REQUESTS " << total_requests_.load() << "\n";
  out << "KEYS " << keys << "\n";
  out << "THREADS " << threads << "\n";

  return out.str();
}
