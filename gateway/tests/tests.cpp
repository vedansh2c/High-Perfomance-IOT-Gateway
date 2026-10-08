#include "protocol.hpp"
#include "thread_pool.hpp"
#include <future>
#include <gtest/gtest.h>
using namespace pulse;
TEST(Protocol, FragmentationAndCoalescing) {
  Frame f{Type::Telemetry, 42, 7, 99, sensor_bytes({20, 40, 90, 12, 77})};
  auto b = encode(f);
  Decoder d;
  std::vector<Frame> frames;
  for (auto c : b) {
    auto v = d.feed({&c, 1});
    frames.insert(frames.end(), v.begin(), v.end());
  }
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].device, 42);
  EXPECT_EQ(frames[0].sequence, 7);
  EXPECT_EQ(decode_sensor(frames[0].payload).humidity, 40);
  auto copy = b;
  b.insert(b.end(), copy.begin(), copy.end());
  EXPECT_EQ(d.feed(b).size(), 2);
}
TEST(Protocol, RejectsMalformed) {
  for (int index : {0, 1, 2, 4, 8}) {
    auto b = encode({Type::Register, 1, 0, 0, {}});
    if (index == 8)
      b[15] = 0;
    else
      b[index] = 255;
    Decoder d;
    EXPECT_THROW(d.feed(b), std::runtime_error);
  }
}
TEST(Protocol, RejectsInvalidSensors) {
  EXPECT_THROW(decode_sensor(sensor_bytes({1, 101, 80, 0, 0})), std::runtime_error);
  EXPECT_THROW(sensor_bytes({1, 2, 3, 4, 5}, 4097), std::runtime_error);
}
TEST(Pool, BoundedQueueDrainsAndCatchesExceptions) {
  std::promise<void> started, release;
  auto future = release.get_future().share();
  ThreadPool pool(1, 1);
  ASSERT_TRUE(pool.submit([&] {
    started.set_value();
    future.wait();
  }));
  started.get_future().wait();
  EXPECT_TRUE(pool.submit([] { throw std::runtime_error("task"); }));
  EXPECT_FALSE(pool.submit([] {}));
  release.set_value();
  pool.shutdown();
  EXPECT_EQ(pool.completed, 2);
  EXPECT_EQ(pool.exceptions, 1);
  EXPECT_EQ(pool.rejected, 1);
  EXPECT_FALSE(pool.submit([] {}));
}
TEST(Pool, ConcurrentWork) {
  std::atomic<int> count{0};
  ThreadPool pool(4, 1000);
  for (int i = 0; i < 1000; ++i)
    ASSERT_TRUE(pool.submit([&] { ++count; }));
  pool.shutdown();
  EXPECT_EQ(count, 1000);
}

#include "metrics.hpp"
TEST(Metrics, ConcurrentUpdatesAndReset) {
  Metrics metrics;
  ThreadPool pool(4, 128);
  for (uint64_t id = 1; id <= 100; ++id) {
    metrics.connect(id);
    ASSERT_TRUE(pool.submit([&metrics, id] {
      Frame f{Type::Telemetry, id, 1, 0, sensor_bytes({22, 45, 90, 12, 77})};
      metrics.process(f, 10);
      ++metrics.messages;
    }));
  }
  pool.shutdown();
  EXPECT_EQ(metrics.processed.load(), 100);
  EXPECT_NE(metrics.json("epoll", true).find("\"processing_p95_us\":15"), std::string::npos);
  metrics.reset();
  EXPECT_EQ(metrics.processed.load(), 0);
  EXPECT_EQ(metrics.messages.load(), 0);
}
TEST(Protocol, MaximumPayloadAndBufferedTail) {
  auto bytes = encode({Type::Telemetry, 1, 1, 0, sensor_bytes({22, 45, 90, 12, 77}, 4096)});
  Decoder decoder;
  EXPECT_TRUE(decoder.feed(std::span(bytes).first(bytes.size() - 1)).empty());
  auto result = decoder.feed(std::span(bytes).last(1));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].payload.size(), 4096);
  EXPECT_EQ(decoder.buffered(), 0);
}
#include "telemetry.hpp"
TEST(Telemetry, ValidatesAndFingerprintsPadding) {
  Frame f{Type::Telemetry, 1, 1, 0, sensor_bytes({22, 45, 90, 12, 77}, 64)};
  auto hash = process_telemetry(f);
  f.payload.back() = 1;
  EXPECT_NE(hash, process_telemetry(f));
  f.payload = sensor_bytes({22, 101, 90, 12, 77});
  EXPECT_THROW(process_telemetry(f), std::runtime_error);
}
TEST(Metrics,ActiveFleetRemainsVisibleAfterLargerRun) {
  Metrics metrics;
  for(uint64_t id=1;id<=500;++id){metrics.connect(id);metrics.disconnect(id);}
  metrics.connect(1);
  auto json=metrics.json("epoll",true);
  EXPECT_NE(json.find("{\"id\":1,\"connected\":true"),std::string::npos);
}
