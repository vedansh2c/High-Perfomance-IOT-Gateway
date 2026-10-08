#include "metrics.hpp"
#include <algorithm>
#include <bit>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>
namespace pulse {
void Metrics::connect(uint64_t id, uint64_t initial_sequence) {
  std::lock_guard lock(mutex_);
  if (devices_.contains(id)) {
    if (devices_.at(id).connected)
      throw std::runtime_error("duplicate active device");
    ++reconnections;
  }
  if (devices_.size() >= 10000 && !devices_.contains(id)) {
    auto it =
        std::find_if(devices_.begin(), devices_.end(), [](auto &d) { return !d.second.connected; });
    if (it != devices_.end())
      devices_.erase(it);
    else
      throw std::runtime_error("device registry full");
  }
  auto &d = devices_[id];
  d.id = id;
  d.connected = true;
  d.errored = false;
  d.sequence = initial_sequence;
  d.heartbeat = monotonic_us();
}
void Metrics::disconnect(uint64_t id, bool error) {
  std::lock_guard lock(mutex_);
  auto it = devices_.find(id);
  if (it != devices_.end()) {
    it->second.connected = false;
    it->second.errored = error;
  }
}
void Metrics::process(const Frame &f, uint64_t elapsed, uint32_t hash) {
  Sensor s{};
  if (f.type == Type::Telemetry)
    s = decode_sensor(f.payload);
  std::lock_guard lock(mutex_);
  auto &d = devices_.at(f.device);
  if (f.sequence > d.sequence + 1)
    missing += f.sequence - d.sequence - 1;
  if (f.sequence <= d.sequence)
    throw std::runtime_error("non-increasing sequence");
  d.sequence = f.sequence;
  ++d.messages;
  if (f.type == Type::Heartbeat)
    d.heartbeat = monotonic_us();
  if (f.type == Type::Telemetry) {
    d.sensor = s;
    d.payload_hash = hash;
    d.has_telemetry = true;
  }
  ++processed;
  ++samples_;
  latency_sum_ += elapsed;
  ++latency_[std::min<size_t>(63, std::bit_width(elapsed))];
}
void Metrics::reset() {
  std::lock_guard lock(mutex_);
  accepted = 0;
  disconnected = 0;
  failed = 0;
  reconnections = 0;
  messages = 0;
  bytes = 0;
  processed = 0;
  invalid = 0;
  rejected = 0;
  overflows = 0;
  errors = 0;
  missing = 0;
  latency_.fill(0);
  samples_ = latency_sum_ = 0;
  epoch_ = monotonic_us();
  for (auto &[id, d] : devices_) {
    d.messages = 0;
  }
}
std::string Metrics::json(const std::string &mode, bool running) const {
  std::lock_guard lock(mutex_);
  auto percentile = [&](double p) {
    uint64_t n = 0, target = uint64_t(double(samples_) * p + 0.999);
    if (!target)
      return uint64_t(0);
    for (size_t i = 0; i < 64; ++i) {
      n += latency_[i];
      if (n >= target)
        return i == 63 ? UINT64_MAX : ((uint64_t(1) << i) - 1);
    }
    return uint64_t(0);
  };
  uint64_t threads = 0;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line))
    if (line.starts_with("Threads:"))
      threads = std::stoull(line.substr(8));
  long rss = 0;
  std::ifstream stat("/proc/self/statm");
  long pages;
  stat >> pages >> rss;
  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  uint64_t fds = 0;
  if (auto *dir = opendir("/proc/self/fd")) {
    while (readdir(dir))
      ++fds;
    closedir(dir);
    fds -= 2;
  }
  std::ostringstream o;
  o << "{\"mode\":\"" << mode << "\",\"running\":" << (running ? "true" : "false")
    << ",\"uptime_s\":" << (monotonic_us() - started_) / 1e6;
#define FIELD(name) o << ",\"" #name "\":" << name.load()
  FIELD(active);
  FIELD(accepted);
  FIELD(disconnected);
  FIELD(failed);
  FIELD(reconnections);
  FIELD(messages);
  FIELD(bytes);
  FIELD(processed);
  FIELD(invalid);
  FIELD(rejected);
  FIELD(overflows);
  FIELD(errors);
  FIELD(missing);
  FIELD(queue);
  FIELD(workers);
  FIELD(queue_capacity);
#undef FIELD
  o << ",\"threads\":" << threads << ",\"metrics_epoch\":" << epoch_;
  o << ",\"processing_avg_us\":" << (samples_ ? double(latency_sum_) / samples_ : 0)
    << ",\"processing_p50_us\":" << percentile(.5) << ",\"processing_p95_us\":" << percentile(.95)
    << ",\"processing_p99_us\":" << percentile(.99)
    << ",\"rss_bytes\":" << rss * sysconf(_SC_PAGESIZE) << ",\"cpu_seconds\":"
    << ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec +
           ru.ru_stime.tv_usec / 1e6
    << ",\"open_fds\":" << fds << ",\"devices\":[";
  bool first = true;
  size_t shown = 0;
  for (bool connected : {true, false}) {
    for (auto &[id, d] : devices_) {
      if (d.connected != connected)
        continue;
      if (shown >= 200)
        break;
      ++shown;
      if (!first)
        o << ',';
      first = false;
      o << "{\"id\":" << id << ",\"connected\":" << (d.connected ? "true" : "false")
        << ",\"errored\":"<<(d.errored?"true":"false")
        << ",\"has_telemetry\":"<<(d.has_telemetry?"true":"false")
        << ",\"messages\":" << d.messages << ",\"sequence\":" << d.sequence
        << ",\"heartbeat_age_s\":" << (monotonic_us() - d.heartbeat) / 1e6
        << ",\"payload_hash\":" << d.payload_hash << ",\"temperature\":" << d.sensor.temperature
        << ",\"humidity\":" << d.sensor.humidity << ",\"battery\":" << d.sensor.battery
        << ",\"latitude\":" << d.sensor.latitude << ",\"longitude\":" << d.sensor.longitude << '}';
    }
  }
  o << "],\"device_count\":" << devices_.size() << '}';
  return o.str();
}
} // namespace pulse
