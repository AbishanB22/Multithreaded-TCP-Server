#include "kvstore.hpp"

void KVStore::set(const std::string& key, const std::string& value) {
  std::unique_lock<std::shared_mutex> lk(mu_);
  map_[key] = value;
}

std::optional<std::string> KVStore::get(const std::string& key) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = map_.find(key);
  if (it == map_.end()) return std::nullopt;
  return it->second;
}

bool KVStore::del(const std::string& key) {
  std::unique_lock<std::shared_mutex> lk(mu_);
  return map_.erase(key) > 0;
}

size_t KVStore::size() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  return map_.size();
}
