#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "blocking_queue.hpp"

class ThreadPool {
 public:
  using Job = std::function<void()>;

  ThreadPool(int threads, size_t queue_cap);
  ~ThreadPool();

  void start();
  void stop();
  bool submit(Job job);

 private:
  void worker_loop();

  int threads_;
  BlockingQueue<Job> q_;
  std::vector<std::thread> workers_;
  std::atomic<bool> running_{false};
};
