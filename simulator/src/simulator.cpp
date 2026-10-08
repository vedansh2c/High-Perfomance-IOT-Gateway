#include "protocol.hpp"
#include "socket.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cmath>
#include <csignal>
#include <deque>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <random>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unordered_map>
using namespace pulse;
static volatile std::sig_atomic_t interrupted = 0;
static void signal_handler(int) { interrupted = 1; }
struct Options {
  std::string host = "127.0.0.1", pattern = "steady";
  int port = 9000, devices = 100, payload = 20, duration = 10, warmup = 0, ramp = 1000, partial = 0,
      slow = 0;
  double rate = 1, drop = 0, malformed = 0;
  bool reconnect = true;
};
struct Output {
  std::vector<uint8_t> bytes;
  size_t offset = 0;
  uint64_t sequence;
  Type type;
  bool measured;
};
struct Pending {
  uint64_t start;
  bool measured;
  Type type;
};
struct Client {
  Fd fd;
  Decoder decoder;
  std::deque<Output> output;
  std::unordered_map<uint64_t, Pending> pending;
  uint64_t sequence = 0, next = 0, heartbeat = 0, opened = 0, retry = 0;
  uint32_t interest = 0;
  bool connecting = false, registered = false, disconnecting = false;
};
struct Stats {
  uint64_t sent = 0, acked = 0, failed = 0, rejected = 0, connects = 0, connection_errors = 0,
           reconnects = 0, bytes = 0, throttled = 0, rtt_sum = 0, samples = 0, abandoned = 0;
  std::array<uint64_t, 64> hist{};
  void sample(uint64_t us) {
    ++samples;
    rtt_sum += us;
    ++hist[std::min<size_t>(63, std::bit_width(us))];
  }
  uint64_t percentile(double p) {
    uint64_t total = 0, target = uint64_t(samples * p + .999);
    if (!target)
      return 0;
    for (size_t i = 0; i < hist.size(); ++i) {
      total += hist[i];
      if (total >= target)
        return (uint64_t(1) << i) - 1;
    }
    return 0;
  }
};
int main(int argc, char **argv) {
  try {
    Options o;
    for (int i = 1; i < argc; ++i) {
      std::string key = argv[i];
      if (i + 1 >= argc)
        throw std::runtime_error("missing argument");
      std::string v = argv[++i];
      if (key == "--host")
        o.host = v;
      else if (key == "--port")
        o.port = std::stoi(v);
      else if (key == "--devices")
        o.devices = std::stoi(v);
      else if (key == "--rate")
        o.rate = std::stod(v);
      else if (key == "--payload")
        o.payload = std::stoi(v);
      else if (key == "--duration")
        o.duration = std::stoi(v);
      else if (key == "--warmup")
        o.warmup = std::stoi(v);
      else if (key == "--ramp")
        o.ramp = std::stoi(v);
      else if (key == "--pattern")
        o.pattern = v;
      else if (key == "--disconnect-probability")
        o.drop = std::stod(v);
      else if (key == "--reconnect")
        o.reconnect = v == "true";
      else if (key == "--partial")
        o.partial = std::stoi(v);
      else if (key == "--slow-readers")
        o.slow = std::stoi(v);
      else if (key == "--malformed")
        o.malformed = std::stod(v);
      else
        throw std::runtime_error("unknown argument " + key);
    }
    if (o.devices < 1 || o.devices > 10000 || o.rate < .1 || o.rate > 1000 ||
        !std::isfinite(o.rate) || o.payload < 20 || o.payload > 4096 || o.duration < 1 ||
        o.duration > 3600 || o.warmup < 0 || o.warmup > 300 || o.ramp < 1 || o.ramp > 10000 ||
        o.port < 1 || o.port > 65535 || o.drop < 0 || o.drop > 1 || !std::isfinite(o.drop) ||
        o.malformed < 0 || o.malformed > 1 || !std::isfinite(o.malformed) || o.partial < 0 ||
        o.partial > 4096 || o.slow < 0 || o.slow > o.devices ||
        (o.pattern != "steady" && o.pattern != "burst"))
      throw std::runtime_error("invalid options");
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    addrinfo hints{}, *resolved = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(o.host.c_str(), std::to_string(o.port).c_str(), &hints, &resolved))
      throw std::runtime_error("host lookup");
    sockaddr_in address = *(sockaddr_in *)resolved->ai_addr;
    freeaddrinfo(resolved);
    Fd ep(epoll_create1(EPOLL_CLOEXEC));
    if (!ep)
      throw std::runtime_error("epoll");
    std::vector<Client> clients(o.devices);
    Stats stats;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> chance(0, 1);
    uint64_t start = monotonic_us(), measurement = start + uint64_t(o.warmup) * 1000000,
             end = measurement + uint64_t(o.duration) * 1000000, last_report = start,
             last_tick = start;
    bool draining = false, reset = o.warmup == 0;
    size_t active = 0;
    size_t buffered_bytes = 0;
    auto update = [&](size_t i) {
      auto &c = clients[i];
      if (!c.fd)
        return;
      epoll_event ev{};
      ev.data.u64 = i;
      ev.events = EPOLLRDHUP;
      if (i >= size_t(o.slow))
        ev.events |= EPOLLIN;
      if (c.connecting || !c.output.empty())
        ev.events |= EPOLLOUT;
      if (c.interest != ev.events) {
        if (epoll_ctl(ep.get(), EPOLL_CTL_MOD, c.fd.get(), &ev))
          throw std::runtime_error("epoll mod");
        c.interest = ev.events;
      }
    };
    auto enqueue = [&](size_t i, Type type) {
      auto &c = clients[i];
      if (c.pending.size() + c.output.size() >= 128) {
        ++stats.throttled;
        ++stats.rejected;
        return false;
      }
      Frame f{type, i + 1, c.sequence++, monotonic_us(), {}};
      if (type == Type::Telemetry)
        f.payload =
            sensor_bytes({float(20 + (i % 100) / 10.0), float(40 + i % 30), float(70 + i % 30),
                          float(12 + (i % 10) * .01), float(77 + (i % 10) * .01)},
                         o.payload);
      auto bytes = encode(f);
      if (buffered_bytes + bytes.size() > 64 * 1024 * 1024) {
        ++stats.throttled;
        ++stats.rejected;
        return false;
      }
      buffered_bytes += bytes.size();
      if (type == Type::Register && chance(rng) < o.malformed)
        bytes[0] = 255;
      c.output.push_back({std::move(bytes), 0, f.sequence, type, reset && !draining});
      return true;
    };
    auto close_client = [&](size_t i, bool error) {
      auto &c = clients[i];
      if (!c.fd)
        return;
      for (auto &[seq, p] : c.pending)
        if (p.measured && p.type == Type::Telemetry)
          ++stats.failed;
      epoll_ctl(ep.get(), EPOLL_CTL_DEL, c.fd.get(), nullptr);
      c.fd = Fd();
      for (auto &out : c.output) {
        buffered_bytes -= out.bytes.size();
        if (out.measured && out.type == Type::Telemetry)
          ++stats.abandoned;
      }
      c.output.clear();
      c.pending.clear();
      c.decoder = Decoder();
      c.registered = false;
      c.connecting = false;
      --active;
      if (error)
        ++stats.connection_errors;
      c.retry = o.reconnect ? monotonic_us() + 500000 : UINT64_MAX;
    };
    auto open_client = [&](size_t i) {
      auto &c = clients[i];
      c.fd = Fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
      if (!c.fd) {
        ++stats.connection_errors;
        c.retry = monotonic_us() + 1000000;
        return;
      }
      int result = connect(c.fd.get(), (sockaddr *)&address, sizeof(address));
      if (result < 0 && errno != EINPROGRESS) {
        c.fd = Fd();
        ++stats.connection_errors;
        c.retry = monotonic_us() + 1000000;
        return;
      }
      if (c.opened)
        ++stats.reconnects;
      ++active;
      c.opened = monotonic_us();
      c.connecting = true;
      c.sequence = 0;
      c.next = c.opened;
      c.heartbeat = c.opened + 5000000;
      c.disconnecting = false;
      epoll_event ev{};
      ev.data.u64 = i;
      ev.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP;
      if (epoll_ctl(ep.get(), EPOLL_CTL_ADD, c.fd.get(), &ev))
        throw std::runtime_error("epoll add");
      c.interest = ev.events;
    };
    auto report = [&](bool final) {
      struct rusage ru {};
      getrusage(RUSAGE_SELF, &ru);
      double elapsed =
          double(monotonic_us() > measurement ? monotonic_us() - measurement : 0) / 1e6;
      std::cout << "{\"final\":" << (final ? "true" : "false") << ",\"elapsed_s\":" << elapsed
                << ",\"active\":" << active << ",\"sent\":" << stats.sent
                << ",\"acknowledged\":" << stats.acked << ",\"failed\":" << stats.failed
                << ",\"queued_abandoned\":" << stats.abandoned
                << ",\"unacknowledged\":" << (stats.sent - stats.acked - stats.failed)
                << ",\"rejected\":" << stats.rejected << ",\"throttled\":" << stats.throttled
                << ",\"connections\":" << stats.connects
                << ",\"connection_errors\":" << stats.connection_errors
                << ",\"reconnections\":" << stats.reconnects << ",\"bytes_sent\":" << stats.bytes
                << ",\"rtt_avg_us\":" << (stats.samples ? double(stats.rtt_sum) / stats.samples : 0)
                << ",\"rtt_p50_us\":" << stats.percentile(.5)
                << ",\"rtt_p95_us\":" << stats.percentile(.95)
                << ",\"rtt_p99_us\":" << stats.percentile(.99) << ",\"cpu_seconds\":"
                << ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec +
                       ru.ru_stime.tv_usec / 1e6
                << ",\"peak_rss_bytes\":" << ru.ru_maxrss * 1024 << "}\n"
                << std::flush;
    };
    epoll_event events[512];
    while (true) {
      uint64_t now = monotonic_us();
      if (!reset && now >= measurement) {
        stats = Stats();
        reset = true;
      }
      if ((now >= end || interrupted) && !draining) {
        draining = true;
        end = now;
        for (size_t i = 0; i < clients.size(); ++i)
          if (clients[i].fd && clients[i].registered) {
            enqueue(i, Type::Disconnect);
            clients[i].disconnecting = true;
            update(i);
          }
      }
      if (draining && (active == 0 || now > end + 2000000))
        break;
      double dt = double(now - last_tick) / 1e6;
      last_tick = now;
      for (size_t i = 0; i < clients.size(); ++i) {
        auto &c = clients[i];
        if (!c.fd) {
          if (!draining && now >= c.retry && double(i) / o.ramp <= double(now - start) / 1e6)
            open_client(i);
          continue;
        }
        if (c.connecting && now - c.opened > 5000000) {
          close_client(i, true);
          continue;
        }
        if (draining)
          continue;
        if (chance(rng) < o.drop * dt) {
          close_client(i, false);
          continue;
        }
        if (!c.registered)
          continue;
        if (now >= c.heartbeat) {
          enqueue(i, Type::Heartbeat);
          c.heartbeat = now + 5000000;
        }
        bool burst = o.pattern == "burst";
        if (now >= c.next && (!burst || (now - start) % 1000000 < 200000)) {
          enqueue(i, Type::Telemetry);
          c.next = now + uint64_t(1000000 / (o.rate * (burst ? 5 : 1)));
        }
        update(i);
      }
      int n = epoll_wait(ep.get(), events, 512, 2);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        throw std::runtime_error("epoll wait");
      }
      for (int k = 0; k < n; ++k) {
        size_t i = events[k].data.u64;
        auto &c = clients[i];
        if (!c.fd)
          continue;
        auto flags = events[k].events;
        if (c.connecting && (flags & (EPOLLOUT | EPOLLERR | EPOLLHUP))) {
          int error = 0;
          socklen_t len = sizeof(error);
          if (getsockopt(c.fd.get(), SOL_SOCKET, SO_ERROR, &error, &len) || error) {
            close_client(i, true);
            continue;
          }
          c.connecting = false;
          ++stats.connects;
          enqueue(i, Type::Register);
        }
        if (flags & EPOLLOUT) {
          bool error = false;
          while (!c.output.empty()) {
            auto &out = c.output.front();
            size_t size = out.bytes.size() - out.offset;
            if (o.partial)
              size = std::min<size_t>(size, o.partial);
            auto sent = send(c.fd.get(), out.bytes.data() + out.offset, size, MSG_NOSIGNAL);
            if (sent > 0) {
              out.offset += sent;
              stats.bytes += sent;
              if (out.offset == out.bytes.size()) {
                c.pending.emplace(out.sequence, Pending{monotonic_us(), out.measured, out.type});
                if (out.measured && out.type == Type::Telemetry)
                  ++stats.sent;
                if (out.type == Type::Register && i < size_t(o.slow))
                  c.registered = true;
                buffered_bytes -= out.bytes.size();
                c.output.pop_front();
              }
              if (o.partial)
                break;
            } else if (sent < 0 && errno == EINTR)
              continue;
            else if (sent < 0 && errno == EAGAIN)
              break;
            else {
              error = true;
              break;
            }
          }
          if (error) {
            close_client(i, true);
            continue;
          }
        }
        if ((flags & EPOLLIN) && i >= size_t(o.slow)) {
          bool error = false;
          uint8_t buffer[8192];
          for (;;) {
            auto count = recv(c.fd.get(), buffer, sizeof(buffer), 0);
            if (count > 0) {
              try {
                for (auto &f : c.decoder.feed({buffer, size_t(count)})) {
                  if (f.type != Type::Ack || f.device != i + 1)
                    throw std::runtime_error("invalid ack");
                  auto p = c.pending.find(f.sequence);
                  if (p == c.pending.end())
                    throw std::runtime_error("unknown ack");
                  if (p->second.type == Type::Register) {
                    c.registered = true;
                    c.next = monotonic_us();
                  }
                  if (p->second.measured && p->second.type == Type::Telemetry) {
                    ++stats.acked;
                    stats.sample(monotonic_us() - p->second.start);
                  }
                  c.pending.erase(p);
                }
              } catch (...) {
                error = true;
                break;
              }
            } else if (count == 0) {
              error = true;
              break;
            } else if (errno == EINTR)
              continue;
            else if (errno == EAGAIN)
              break;
            else {
              error = true;
              break;
            }
          }
          if (error) {
            close_client(i, !c.disconnecting);
            continue;
          }
        }
        if (flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
          close_client(i, !c.disconnecting);
          continue;
        }
        update(i);
      }
      if (now - last_report >= 1000000) {
        report(false);
        last_report = now;
      }
    }
    for (size_t i = 0; i < clients.size(); ++i)
      close_client(i, false);
    report(true);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
