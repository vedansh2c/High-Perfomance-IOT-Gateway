#include "protocol.hpp"
#include "socket.hpp"
#include <arpa/inet.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
using namespace pulse;
int main(int argc, char **argv) {
  try {
    int port = argc > 1 ? std::stoi(argv[1]) : 9000;
    Fd fd(socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd.get(), (sockaddr *)&a, sizeof(a)))
      throw std::runtime_error("connect");
    Decoder decoder;
    for (uint64_t i = 0; i < 3; ++i) {
      Frame f{i == 0 ? Type::Register : (i == 1 ? Type::Telemetry : Type::Disconnect), 1, i,
              monotonic_us(), i == 1 ? sensor_bytes({22, 45, 90, 12, 77}) : std::vector<uint8_t>{}};
      auto b = encode(f);
      for (size_t pos = 0; pos < b.size();) {
        auto n = send(fd.get(), b.data() + pos, 1, 0);
        if (n <= 0)
          throw std::runtime_error("write");
        pos += n;
      }
      bool ack = false;
      while (!ack) {
        pollfd p{fd.get(), POLLIN, 0};
        if (poll(&p, 1, 2000) <= 0)
          throw std::runtime_error("ack timeout");
        uint8_t buf[128];
        auto n = recv(fd.get(), buf, sizeof(buf), 0);
        if (n <= 0)
          throw std::runtime_error("EOF");
        for (auto &r : decoder.feed({buf, size_t(n)}))
          if (r.type == Type::Ack && r.sequence == i)
            ack = true;
      }
    }
    std::cout << "registered, telemetry acknowledged, graceful disconnect\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
