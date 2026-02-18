#include "thread_pool.hpp"

ThreadPool::ThreadPool(int threads, size_t queue_cap)
    : threads_(threads), q_(queue_cap) {}

ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::start() {
  running_.store(true);

  for (int i = 0; i < threads_; i++) {
    workers_.emplace_back([this]() {
      while (running_.load()) {
        auto job = q_.pop();
        if (!job.has_value()) break;
        (*job)();
      }
    });
  }
}

void ThreadPool::stop() {
  if (!running_.exchange(false)) return;

  q_.close();

  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }

  workers_.clear();
}

bool ThreadPool::submit(Job job) { return q_.push(std::move(job)); }
