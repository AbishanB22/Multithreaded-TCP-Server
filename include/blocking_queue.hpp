#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

template <typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(size_t capacity) : capacity_(capacity) {}

  // Returns false if queue is closed.
  bool push(T item) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_not_full_.wait(lk, [&] { return closed_ || q_.size() < capacity_; });
    if (closed_) return false;
    q_.push_back(std::move(item));
    cv_not_empty_.notify_one();
    return true;
  }

  // Returns nullopt if closed and empty.
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
