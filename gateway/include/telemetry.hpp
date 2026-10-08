#pragma once
#include "protocol.hpp"
namespace pulse {
// Validate sensor fields and compute a diagnostic payload fingerprint.
// This is not a wire checksum or authentication mechanism.
uint32_t process_telemetry(const Frame &frame);
} // namespace pulse
