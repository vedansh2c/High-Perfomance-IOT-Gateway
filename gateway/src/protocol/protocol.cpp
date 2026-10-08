#include "protocol.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
namespace pulse {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static void put(std::vector<uint8_t> &b, uint64_t v, int n) {
  for (int i = n - 1; i >= 0; --i)
    b.push_back(uint8_t(v >> (8 * i)));
}
static uint64_t get(std::span<const uint8_t> b, size_t p, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; ++i)
    v = (v << 8) | b[p + i];
  return v;
}
uint64_t monotonic_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
std::vector<uint8_t> encode(const Frame &f) {
  if (!f.device || f.payload.size() > max_payload)
    throw std::runtime_error("invalid frame");
  std::vector<uint8_t> b;
  b.reserve(header_size + f.payload.size());
  put(b, 1, 1);
  put(b, uint8_t(f.type), 1);
  put(b, 0, 2);
  put(b, f.payload.size(), 4);
  put(b, f.device, 8);
  put(b, f.sequence, 8);
  put(b, f.timestamp, 8);
  b.insert(b.end(), f.payload.begin(), f.payload.end());
  return b;
}
std::vector<uint8_t> sensor_bytes(const Sensor &s, size_t size) {
  if (size < 20 || size > max_payload)
    throw std::runtime_error("payload must be 20..4096");
  std::vector<uint8_t> b;
  for (float v : {s.temperature, s.humidity, s.battery, s.latitude, s.longitude})
    put(b, std::bit_cast<uint32_t>(v), 4);
  b.resize(size, 0);
  return b;
}
Sensor decode_sensor(std::span<const uint8_t> p) {
  if (p.size() < 20)
    throw std::runtime_error("short telemetry");
  float v[5];
  for (int i = 0; i < 5; ++i) {
    v[i] = std::bit_cast<float>(uint32_t(get(p, i * 4, 4)));
    if (!std::isfinite(v[i]))
      throw std::runtime_error("non-finite sensor");
  }
  if (v[1] < 0 || v[1] > 100 || v[2] < 0 || v[2] > 100 || v[3] < -90 || v[3] > 90 || v[4] < -180 ||
      v[4] > 180)
    throw std::runtime_error("sensor outside range");
  return {v[0], v[1], v[2], v[3], v[4]};
}
std::vector<Frame> Decoder::feed(std::span<const uint8_t> bytes) {
  if (buffer_.size() + bytes.size() > 65536)
    throw std::runtime_error("framing buffer limit");
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<Frame> out;
  size_t pos = 0;
  while (buffer_.size() - pos >= header_size) {
    auto b = std::span<const uint8_t>(buffer_).subspan(pos);
    auto len = get(b, 4, 4);
    auto type = b[1];
    if (b[0] != 1 || get(b, 2, 2) != 0 || type < 1 || type > 5 || len > max_payload ||
        get(b, 8, 8) == 0)
      throw std::runtime_error("malformed header");
    if (type != 2 && len != 0)
      throw std::runtime_error("unexpected payload");
    if (type == 2 && len < 20)
      throw std::runtime_error("short telemetry");
    if (b.size() < header_size + len)
      break;
    out.push_back({Type(type),
                   get(b, 8, 8),
                   get(b, 16, 8),
                   get(b, 24, 8),
                   {b.begin() + header_size, b.begin() + header_size + len}});
    pos += header_size + len;
  }
  buffer_.erase(buffer_.begin(), buffer_.begin() + pos);
  return out;
}
} // namespace pulse
