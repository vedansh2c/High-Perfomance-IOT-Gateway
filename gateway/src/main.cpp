#include "server.hpp"
#include "socket.hpp"
#include <csignal>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
using namespace pulse;
static std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
static void signal_handler(int) { interrupted.store(true, std::memory_order_relaxed); }
int main(int argc, char **argv) {
  try {
    Config config;
    std::string mode = "epoll", ipc = "/tmp/pulsegate.sock";
    for (int i = 1; i < argc; ++i) {
      std::string k = argv[i];
      if (i + 1 == argc)
        throw std::runtime_error("missing argument");
      std::string v = argv[++i];
      if (k == "--mode")
        mode = v;
      else if (k == "--port")
        config.port = std::stoi(v);
      else if (k == "--workers")
        config.worker_count = std::stoul(v);
      else if (k == "--max-connections")
        config.max_connections = std::stoul(v);
      else if (k == "--queue")
        config.queue_capacity = std::stoul(v);
      else if (k == "--ack-delay-ms")
        config.ack_delay_ms = std::stoi(v);
      else if (k == "--idle-timeout-ms")
        config.idle_timeout_ms = std::stoi(v);
      else if (k == "--ipc")
        ipc = v;
      else
        throw std::runtime_error("unknown argument");
    }
    if ((mode != "baseline" && mode != "epoll") || !config.worker_count ||
        config.worker_count > 64 || !config.queue_capacity || !config.max_connections ||
        config.max_connections > 10000 || config.port < 1 || config.port > 65535 ||
        config.ack_delay_ms < 0 || config.ack_delay_ms > 1000 || config.idle_timeout_ms < 50 ||
        config.idle_timeout_ms > 600000)
      throw std::runtime_error("invalid config");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    Fd control(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (ipc.size() >= sizeof(addr.sun_path))
      throw std::runtime_error("IPC path too long");
    std::copy(ipc.begin(), ipc.end(), addr.sun_path);
    unlink(ipc.c_str());
    if (bind(control.get(), (sockaddr *)&addr, sizeof(addr)) || listen(control.get(), 16))
      throw std::runtime_error("IPC bind");
    chmod(ipc.c_str(), 0660);
    nonblock(control.get());
    Metrics metrics;
    std::atomic<bool> stopping{false}, server_stop{false}, running{false};
    std::mutex config_mutex;
    std::string requested_mode = mode;
    size_t requested_workers = config.worker_count;
    bool restart = false;
    std::thread ipc_thread([&] {
      while (!stopping) {
        pollfd p{control.get(), POLLIN, 0};
        if (poll(&p, 1, 100) <= 0)
          continue;
        Fd peer(accept4(control.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (!peer)
          continue;
        timeval timeout{1, 0};
        setsockopt(peer.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(peer.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        char b[256];
        std::string command;
        while (command.size() < 256) {
          auto n = recv(peer.get(), b, sizeof(b), 0);
          if (n <= 0)
            break;
          command.append(b, n);
          if (command.find('\n') != std::string::npos)
            break;
        }
        std::string result;
        try {
          std::lock_guard lock(config_mutex);
          if (command == "metrics\n" || command == "status\n")
            result = metrics.json(mode, running);
          else if (command == "reset\n") {
            if (metrics.active)
              result = "{\"error\":\"stop devices before reset\"}";
            else {
              metrics.reset();
              result = "{\"ok\":true}";
            }
          } else if (command.starts_with("configure ")) {
            std::istringstream in(command);
            std::string cmd, newmode;
            size_t workers;
            in >> cmd >> newmode >> workers;
            if (!in || (newmode != "epoll" && newmode != "baseline") || workers < 1 || workers > 64)
              result = "{\"error\":\"invalid configuration\"}";
            else {
              requested_mode = newmode;
              requested_workers = workers;
              restart = true;
              server_stop = true;
              result = "{\"ok\":true,\"restarting\":true}";
            }
          } else
            result = "{\"error\":\"unknown command\"}";
        } catch (const std::exception &e) {
          result = "{\"error\":\"control failure\"}";
        }
        result += '\n';
        size_t offset = 0;
        while (offset < result.size()) {
          auto n = send(peer.get(), result.data() + offset, result.size() - offset, MSG_NOSIGNAL);
          if (n > 0)
            offset += n;
          else if (errno == EINTR)
            continue;
          else
            break;
        }
      }
    });
    std::thread signal_watch([&] {
      while (!stopping) {
        if (interrupted.load(std::memory_order_relaxed)) {
          server_stop = true;
          stopping = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
    int code = 0;
    while (!stopping) {
      server_stop = false;
      running = true;
      try {
        if (mode == "baseline")
          serve_baseline(config, metrics, server_stop);
        else
          serve_epoll(config, metrics, server_stop);
      } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        code = 1;
        stopping = true;
      }
      running = false;
      std::lock_guard lock(config_mutex);
      if (restart && !stopping) {
        mode = requested_mode;
        config.worker_count = requested_workers;
        restart = false;
      } else
        stopping = true;
    }
    stopping = true;
    ipc_thread.join();
    signal_watch.join();
    unlink(ipc.c_str());
    return code;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
