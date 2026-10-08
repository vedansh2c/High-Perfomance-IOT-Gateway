#include "server.hpp"
#include "socket.hpp"
#include "telemetry.hpp"
#include "thread_pool.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <unordered_map>
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif
namespace pulse {
namespace {
Fd listener(int port) {
  Fd fd(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd)
    throw std::runtime_error("socket failed");
  int yes = 1;
  setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = INADDR_ANY;
  if (bind(fd.get(), (sockaddr *)&a, sizeof(a)) || listen(fd.get(), 512))
    throw std::runtime_error("bind/listen failed");
  nonblock(fd.get());
  return fd;
}
struct Connection {
  Fd fd;
  Decoder decoder;
  uint64_t id = 0, token = 0, last = monotonic_us(), sequence = 0;
  std::deque<Frame> pending;
  std::vector<uint8_t> output;
  size_t offset = 0;
  uint32_t interest = 0;
  bool inflight = false, closing = false, eof = false, error = false;
  explicit Connection(int f) : fd(f) {}
};
bool validate(Connection &c, const Frame &f, Metrics &m) {
  if (c.closing)
    return false;
  if (f.type == Type::Register) {
    if (c.id)
      return false;
    m.connect(f.device, f.sequence);
    c.id = f.device;
    c.sequence = f.sequence;
    return true;
  }
  if (!c.id || f.device != c.id || f.type == Type::Ack || f.sequence <= c.sequence)
    return false;
  c.sequence = f.sequence;
  return true;
}
void ack(Connection &c, const Frame &f) {
  auto b = encode({Type::Ack, f.device, f.sequence, f.timestamp, {}});
  if (c.output.size() - c.offset + b.size() > 65536)
    throw std::runtime_error("slow reader buffer limit");
  if (c.offset) {
    c.output.erase(c.output.begin(), c.output.begin() + c.offset);
    c.offset = 0;
  }
  c.output.insert(c.output.end(), b.begin(), b.end());
}
bool write_output(Connection &c) {
  while (c.offset < c.output.size()) {
    auto n = send(c.fd.get(), c.output.data() + c.offset, c.output.size() - c.offset, MSG_NOSIGNAL);
    if (n > 0)
      c.offset += n;
    else if (n < 0 && errno == EINTR)
      continue;
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return true;
    else
      return false;
  }
  c.output.clear();
  c.offset = 0;
  return true;
}
bool read_frames(Connection &c, Metrics &m) {
  uint8_t b[8192];
  auto n = recv(c.fd.get(), b, sizeof(b), 0);
  if (n > 0) {
    m.bytes += n;
    c.last = monotonic_us();
    auto frames = c.decoder.feed({b, size_t(n)});
    if (c.pending.size() + frames.size() > 512)
      throw std::runtime_error("pending frame limit");
    for (auto &f : frames) {
      ++m.messages;
      c.pending.push_back(std::move(f));
    }
    return true;
  }
  if (n == 0) {
    c.eof = true;
    if (c.decoder.buffered())
      throw std::runtime_error("truncated frame at EOF");
    return true;
  }
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
void gone(Connection &c, Metrics &m) {
  if (c.id)
    m.disconnect(c.id,c.error);
  --m.active;
  ++m.disconnected;
}
} // namespace
void serve_baseline(const Config &config, Metrics &metrics, std::atomic<bool> &stop) {
  auto listenfd = listener(config.port);
  metrics.workers = 0;
  metrics.queue_capacity = 0;
  struct ClientThread {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::vector<ClientThread> clients;
  clients.reserve(config.max_connections);
  while (!stop) {
    for (auto it = clients.begin(); it != clients.end();) {
      if (*it->done) {
        it->thread.join();
        it = clients.erase(it);
      } else
        ++it;
    }
    pollfd p{listenfd.get(), POLLIN, 0};
    if (poll(&p, 1, 100) <= 0)
      continue;
    int raw = accept4(listenfd.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (raw < 0) {
      if (errno != EAGAIN && errno != EINTR)
        ++metrics.failed;
      continue;
    }
    if (clients.size() >= config.max_connections) {
      close(raw);
      ++metrics.failed;
      continue;
    }
    ++metrics.accepted;
    ++metrics.active;
    auto done = std::make_shared<std::atomic<bool>>(false);
    try {
      clients.push_back(
          {std::thread([&, raw, done] {
             Connection c(raw);
             try {
               uint64_t shutdown_deadline = 0;
               while (true) {
                 if (stop && !shutdown_deadline)
                   shutdown_deadline = monotonic_us() + 2000000;
                 if (shutdown_deadline && (monotonic_us() >= shutdown_deadline ||
                                           (c.pending.empty() && c.output.empty())))
                   break;
                 pollfd p{c.fd.get(),
                          short((c.eof || stop ? 0 : POLLIN) | (c.output.empty() ? 0 : POLLOUT)),
                          0};
                 int r = poll(&p, 1, 100);
                 if (r < 0 && errno == EINTR)
                   continue;
                 if (r < 0) {++metrics.errors;c.error=true;break;}
                 if (monotonic_us() - c.last > uint64_t(config.idle_timeout_ms) * 1000)
                   break;
                 if (p.revents & (POLLERR | POLLNVAL)) {++metrics.errors;c.error=true;break;}
                 if (!stop && !c.eof && (p.revents & (POLLIN | POLLHUP))) {
                   if (!read_frames(c, metrics)) {++metrics.errors;c.error=true;break;}
                 }
                 if (p.revents & POLLOUT) {
                   if (!write_output(c)) {++metrics.errors;c.error=true;break;}
                 }
                 while (!c.pending.empty()) {
                   if (stop && !shutdown_deadline)
                     shutdown_deadline = monotonic_us() + 2000000;
                   if (shutdown_deadline && monotonic_us() >= shutdown_deadline)
                     break;
                   Frame f = std::move(c.pending.front());
                   c.pending.pop_front();
                   if (!validate(c, f, metrics))
                     throw std::runtime_error("session violation");
                   auto start = monotonic_us();
                   auto hash = process_telemetry(f);
                   if (f.type != Type::Register)
                     metrics.process(f, monotonic_us() - start, hash);
                   if (config.ack_delay_ms && !stop)
                     std::this_thread::sleep_for(std::chrono::milliseconds(config.ack_delay_ms));
                   ack(c, f);
                   if (f.type == Type::Disconnect)
                     c.closing = true;
                 }
                 if (!write_output(c)) {++metrics.errors;c.error=true;break;}
                 if ((c.closing || c.eof) && c.output.empty())
                   break;
               }
             } catch (...) {
               c.error=true;
               ++metrics.invalid;
             }
             gone(c, metrics);
             *done = true;
           }),
           done});
    } catch (...) {
      close(raw);
      --metrics.active;
      ++metrics.failed;
    }
  }
  for (auto &c : clients)
    c.thread.join();
}
#ifdef __linux__
void serve_epoll(const Config &config, Metrics &metrics, std::atomic<bool> &stop) {
  auto listenfd = listener(config.port);
  Fd ep(epoll_create1(EPOLL_CLOEXEC)), wake(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
  if (!ep || !wake)
    throw std::runtime_error("epoll/eventfd failed");
  auto set = [&](int op, int fd, uint64_t token, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.u64 = token;
    if (epoll_ctl(ep.get(), op, fd, &ev) < 0)
      throw std::runtime_error("epoll_ctl failed");
  };
  set(EPOLL_CTL_ADD, listenfd.get(), 0, EPOLLIN);
  set(EPOLL_CTL_ADD, wake.get(), 1, EPOLLIN);
  ThreadPool pool(config.worker_count, config.queue_capacity);
  metrics.workers = config.worker_count;
  metrics.queue_capacity = config.queue_capacity;
  struct Completion {
    uint64_t token;
    Frame frame;
    uint64_t elapsed;
    bool valid;
    uint32_t hash;
  };
  std::mutex completion_mutex;
  std::deque<Completion> completions;
  std::unordered_map<uint64_t, std::shared_ptr<Connection>> clients;
  uint64_t token = 2;
  auto close_client = [&](uint64_t id) {
    auto it = clients.find(id);
    if (it != clients.end()) {
      epoll_ctl(ep.get(), EPOLL_CTL_DEL, it->second->fd.get(), nullptr);
      gone(*it->second, metrics);
      clients.erase(it);
    }
  };
  auto arm = [&](Connection &c) {
    uint32_t ev = c.eof ? 0u : static_cast<uint32_t>(EPOLLRDHUP);
    if (!stop && !c.inflight && c.pending.empty() && !c.closing && !c.eof && c.output.empty())
      ev |= EPOLLIN;
    if (!c.output.empty())
      ev |= EPOLLOUT;
    if (c.interest != ev) {
      set(EPOLL_CTL_MOD, c.fd.get(), c.token, ev);
      c.interest = ev;
    }
  };
  auto dispatch = [&](Connection &c) {
    if (c.inflight || c.pending.empty())
      return;
    Frame f = c.pending.front(); // retry safely when the bounded queue is full
    bool submitted =
        pool.submit([&, f = std::move(f), id = c.token, start = monotonic_us()]() mutable {
          bool valid = true;
          uint32_t hash = 0;
          try {
            hash = process_telemetry(f);
          } catch (...) {
            valid = false;
          }
          uint64_t elapsed = monotonic_us() - start;
          if (config.ack_delay_ms && !stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(config.ack_delay_ms));
          {
            std::lock_guard lock(completion_mutex);
            completions.push_back({id, std::move(f), elapsed, valid, hash});
          }
          uint64_t one = 1;
          auto ignored = write(wake.get(), &one, sizeof(one));
          (void)ignored;
        });
    if (submitted) {
      c.inflight = true;
      c.pending.pop_front();
    } else {
      ++metrics.rejected;
      ++metrics.overflows;
    }
  };
  epoll_event events[256];
  uint64_t shutdown_deadline = 0;
  try {
    while (true) {
      if (stop && !shutdown_deadline) {
        shutdown_deadline = monotonic_us() + 2000000;
        epoll_ctl(ep.get(), EPOLL_CTL_DEL, listenfd.get(), nullptr);
      }
      if (shutdown_deadline && (clients.empty() || monotonic_us() >= shutdown_deadline))
        break;
      int count = epoll_wait(ep.get(), events, 256, 50);
      if (count < 0) {
        if (errno == EINTR)
          continue;
        throw std::runtime_error("epoll_wait failed");
      }
      for (int i = 0; i < count; ++i) {
        auto id = events[i].data.u64;
        auto flags = events[i].events;
        if (id == 0) {
          if (stop)
            continue;
          for (int a = 0; a < 256; ++a) {
            int fd = accept4(listenfd.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
              if (errno == EINTR)
                continue;
              if (errno != EAGAIN)
                ++metrics.failed;
              break;
            }
            if (clients.size() >= config.max_connections) {
              close(fd);
              ++metrics.failed;
              continue;
            }
            auto c = std::make_shared<Connection>(fd);
            c->token = token++;
            set(EPOLL_CTL_ADD, fd, c->token, EPOLLIN | EPOLLRDHUP);
            c->interest = EPOLLIN | EPOLLRDHUP;
            clients[c->token] = c;
            ++metrics.accepted;
            ++metrics.active;
          }
          continue;
        }
        if (id == 1) {
          uint64_t n;
          while (read(wake.get(), &n, sizeof(n)) > 0) {
          }
          std::deque<Completion> ready;
          {
            std::lock_guard lock(completion_mutex);
            ready.swap(completions);
          }
          for (auto &result : ready) {
            auto it = clients.find(result.token);
            if (it == clients.end())
              continue;
            auto &c = *it->second;
            c.inflight = false;
            try {
              if (!result.valid || !validate(c, result.frame, metrics))
                throw std::runtime_error("invalid session or sensor");
              if (result.frame.type != Type::Register)
                metrics.process(result.frame, result.elapsed, result.hash);
              ack(c, result.frame);
              if (result.frame.type == Type::Disconnect)
                c.closing = true;
            } catch (...) {
              ++metrics.invalid;
              c.error=true;
              close_client(result.token);
            }
          }
          continue;
        }
        auto it = clients.find(id);
        if (it == clients.end())
          continue;
        auto &c = *it->second;
        try {
          if (flags & EPOLLERR) {
            ++metrics.errors;
            c.error=true;
            close_client(id);
            continue;
          }
          if (!stop && !c.eof && (flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
            if (!read_frames(c, metrics)) {
              ++metrics.errors;
              c.error=true;
              close_client(id);
              continue;
            }
          }
          if (flags & EPOLLOUT) {
            if (!write_output(c)) {
              ++metrics.errors;
              c.error=true;
              close_client(id);
              continue;
            }
          }
        } catch (...) {
          ++metrics.invalid;
          c.error=true;
          close_client(id);
        }
      }
      std::vector<uint64_t> expired;
      for (auto &[id, c] : clients) {
        if (monotonic_us() - c->last > uint64_t(config.idle_timeout_ms) * 1000 ||
            ((c->closing || c->eof || stop) && c->output.empty() && !c->inflight &&
             c->pending.empty())) {
          expired.push_back(id);
          continue;
        }
        dispatch(*c);
        arm(*c);
      }
      for (auto id : expired)
        close_client(id);
      metrics.queue = pool.depth();
    }
  } catch (...) {
    stop = true;
    pool.shutdown();
    for (auto &[id, c] : clients)
      gone(*c, metrics);
    throw;
  }
  pool.shutdown();
  for (auto &[id, c] : clients)
    gone(*c, metrics);
  metrics.queue = 0;
}
#else
void serve_epoll(const Config &, Metrics &, std::atomic<bool> &) {
  throw std::runtime_error("epoll requires Linux");
}
#endif
} // namespace pulse
