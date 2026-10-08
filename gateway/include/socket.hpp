#pragma once
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>
namespace pulse {
class Fd {
  int fd_ = -1;

public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  ~Fd() {
    if (fd_ >= 0)
      ::close(fd_);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  Fd(Fd &&o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
  Fd &operator=(Fd &&o) noexcept {
    if (this != &o) {
      if (fd_ >= 0)
        ::close(fd_);
      fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
  }
  int get() const { return fd_; }
  explicit operator bool() const { return fd_ >= 0; }
};
inline void nonblock(int fd) {
  int f = fcntl(fd, F_GETFL, 0);
  if (f < 0 || fcntl(fd, F_SETFL, f | O_NONBLOCK) < 0)
    throw std::runtime_error("fcntl");
}
} // namespace pulse
