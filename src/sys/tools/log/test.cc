// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/wire_format.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/sys/tools/log/log.h"

namespace log {

class FakeLogSink : public fuchsia::logger::LogSink {
 public:
  FakeLogSink(fidl::InterfaceRequest<fuchsia::logger::LogSink> request)
      : binding_(this, std::move(request)) {}
  void Connect(zx::socket socket) override { socket_ = std::move(socket); }
  fit::result<std::tuple<zx_time_t, std::string, std::string>, zx_status_t> ReadPacket() {
    static_assert(FX_LOG_MAX_DATAGRAM_LEN == sizeof(fx_log_packet_t));
    std::vector<char> packet(FX_LOG_MAX_DATAGRAM_LEN);
    size_t actual = 0;
    zx_status_t status = socket_.read(0, packet.data(), packet.size(), &actual);
    if (status != ZX_OK) {
      return fit::error(status);
    }
    if (actual != packet.size()) {
      // Packet should be exactly one log packet.
      return fit::error(ZX_ERR_BAD_STATE);
    }
    if (*packet.rbegin() != 0) {
      // Non-zero final byte indicates an improperly terminated message string.
      return fit::error(ZX_ERR_BAD_STATE);
    }
    auto log_packet = reinterpret_cast<const fx_log_packet_t*>(packet.data());
    if (log_packet->data[0] == 0 || log_packet->data[1 + log_packet->data[0]] != 0) {
      // This fake assumes messages have exactly one tag.
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    std::string tag(&log_packet->data[1]);
    std::string msg(&log_packet->data[1 + tag.length() + 1]);
    return fit::ok(std::make_tuple(log_packet->metadata.time, tag, msg));
  }

 private:
  fidl::Binding<fuchsia::logger::LogSink> binding_;
  zx::socket socket_;
};

class LogTest : public gtest::TestLoopFixture {
 protected:
  LogTest() = default;
};

TEST_F(LogTest, InvalidArgc) {
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 0, nullptr), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 1, nullptr), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 2, nullptr), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 4, nullptr), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 5, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST_F(LogTest, TagTooLong) {
  std::string tag(64, 'x');
  const char* argv[]{"log", tag.c_str(), ""};
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 3, const_cast<char**>(argv)),
            ZX_ERR_OUT_OF_RANGE);
}

TEST_F(LogTest, CombinedTooLong) {
  std::string tag(32, 'x');
  std::string msg(32716, 'x');
  const char* argv[]{"log", tag.c_str(), msg.c_str()};
  EXPECT_EQ(log::ParseAndWriteLog(nullptr, zx::time::infinite(), 3, const_cast<char**>(argv)),
            ZX_ERR_OUT_OF_RANGE);
}

TEST_F(LogTest, SimpleLog) {
  fuchsia::logger::LogSinkHandle log_sink;
  FakeLogSink fake(log_sink.NewRequest());
  const char* argv[]{"log", "hello", "world"};
  auto time = zx::clock::get_monotonic();
  EXPECT_EQ(log::ParseAndWriteLog(std::move(log_sink), time, 3, const_cast<char**>(argv)), ZX_OK);
  RunLoopUntilIdle();
  auto result = fake.ReadPacket();
  ASSERT_TRUE(result.is_ok()) << zx_status_get_string(result.error());
  auto [time_out, tag, msg] = result.take_value();
  EXPECT_EQ(tag, "hello");
  EXPECT_EQ(msg, "world");
  EXPECT_EQ(time_out, time.get());
}

}  // namespace log
