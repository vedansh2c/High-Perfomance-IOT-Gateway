#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace pulse {
class ThreadPool {
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  size_t capacity_;
  bool stopping_ = false;

public:
  std::atomic<uint64_t> completed{0}, exceptions{0}, rejected{0};
  ThreadPool(size_t count, size_t capacity) : capacity_(capacity) {
    if (!count || !capacity)
      throw std::invalid_argument("empty pool");
    try {
      for (size_t i = 0; i < count; ++i)
        workers_.emplace_back([this] {
          for (;;) {
            std::function<void()> task;
            {
              std::unique_lock lock(mutex_);
              ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
              if (tasks_.empty() && stopping_)
                return;
              task = std::move(tasks_.front());
              tasks_.pop();
            }
            try {
              task();
            } catch (...) {
              ++exceptions;
            }
            ++completed;
          }
        });
    } catch (...) {
      shutdown();
      throw;
    }
  }
  bool submit(std::function<void()> task) {
    std::lock_guard lock(mutex_);
    if (stopping_ || tasks_.size() >= capacity_) {
      ++rejected;
      return false;
    }
    tasks_.push(std::move(task));
    ready_.notify_one();
    return true;
  }
  size_t depth() {
    std::lock_guard lock(mutex_);
    return tasks_.size();
  }
  void shutdown() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto &t : workers_)
      if (t.joinable())
        t.join();
  }
  ~ThreadPool() { shutdown(); }
};
} // namespace pulse
