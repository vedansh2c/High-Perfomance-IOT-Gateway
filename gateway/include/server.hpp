#pragma once
#include "metrics.hpp"
#include <atomic>
namespace pulse {
struct Config {
  int port = 9000;
  size_t max_connections = 6000, worker_count = 4, queue_capacity = 1024;
  int ack_delay_ms = 0, idle_timeout_ms = 30000;
};
void serve_baseline(const Config &, Metrics &, std::atomic<bool> &);
void serve_epoll(const Config &, Metrics &, std::atomic<bool> &);
} // namespace pulse
