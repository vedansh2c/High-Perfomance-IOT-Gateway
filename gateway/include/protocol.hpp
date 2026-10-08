#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
namespace pulse {
constexpr size_t header_size = 32, max_payload = 4096;
enum class Type : uint8_t { Register = 1, Telemetry = 2, Heartbeat = 3, Ack = 4, Disconnect = 5 };
struct Frame {
  Type type;
  uint64_t device = 0, sequence = 0, timestamp = 0;
  std::vector<uint8_t> payload;
};
struct Sensor {
  float temperature, humidity, battery, latitude, longitude;
};
std::vector<uint8_t> encode(const Frame &f);
std::vector<uint8_t> sensor_bytes(const Sensor &s, size_t size = 20);
Sensor decode_sensor(std::span<const uint8_t> p);
class Decoder {
  std::vector<uint8_t> buffer_;

public:
  std::vector<Frame> feed(std::span<const uint8_t> bytes);
  size_t buffered() const { return buffer_.size(); }
};
uint64_t monotonic_us();
} // namespace pulse
