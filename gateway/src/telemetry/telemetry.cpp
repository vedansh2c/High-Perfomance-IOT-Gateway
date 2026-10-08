#include "telemetry.hpp"
namespace pulse {
uint32_t process_telemetry(const Frame &frame) {
  if (frame.type != Type::Telemetry)
    return 0;
  decode_sensor(frame.payload);
  uint32_t hash = 2166136261u;
  for (auto byte : frame.payload)
    hash = (hash ^ byte) * 16777619u;
  return hash;
}
} // namespace pulse
