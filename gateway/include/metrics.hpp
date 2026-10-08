#pragma once
#include "protocol.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
namespace pulse {
struct Device {
  uint64_t id = 0, messages = 0, sequence = 0, heartbeat = 0;
  bool connected = false, errored = false, has_telemetry = false;
  Sensor sensor{};
  uint32_t payload_hash = 0;
};
class Metrics {
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, Device> devices_;
  std::array<uint64_t, 64> latency_{};
  uint64_t samples_ = 0, latency_sum_ = 0, started_ = monotonic_us(), epoch_ = started_;

public:
  std::atomic<uint64_t> active{0}, accepted{0}, disconnected{0}, failed{0}, reconnections{0},
      messages{0}, bytes{0}, processed{0}, invalid{0}, rejected{0}, overflows{0}, errors{0},
      missing{0}, queue{0}, workers{0}, queue_capacity{0};
  void connect(uint64_t id, uint64_t initial_sequence = 0);
  void disconnect(uint64_t id, bool error = false);
  void process(const Frame &f, uint64_t elapsed, uint32_t hash = 0);
  std::string json(const std::string &mode, bool running) const;
  void reset();
};
} // namespace pulse
