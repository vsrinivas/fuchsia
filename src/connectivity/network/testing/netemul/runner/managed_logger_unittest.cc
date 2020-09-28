// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_logger.h"

#include <lib/gtest/real_loop_fixture.h>

#include "log_listener.h"
#include "log_listener_test_helpers.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/testing/predicates/status.h"

namespace netemul {
namespace testing {

constexpr const char* kLoggerName = "my_logger";

class ManagedLoggerTest : public gtest::RealLoopFixture {
 public:
  using LogLevel = fuchsia::logger::LogLevelFilter;
  ManagedLoggerTest() : gtest::RealLoopFixture(), listener_(new TestListener()) {}

 protected:
  void Startup(std::string name, bool is_err) {
    logger_ = std::make_unique<ManagedLogger>(std::move(name), is_err, listener_);
    sock_.reset(logger_->CreateHandle().release());
    logger_->Start([](ManagedLogger* logger) { FAIL() << "Managed logger shouldn't close"; });
  }

  void Write(const std::string& msg) {
    size_t actual = 0;
    EXPECT_OK(sock_.write(0, msg.c_str(), msg.length(), &actual));
    EXPECT_EQ(actual, msg.length());
  }

  std::vector<fuchsia::logger::LogMessage>& messages() { return listener_->messages(); }

  void WaitForMessageCount(size_t len) {
    RunLoopUntil([this, len]() { return messages().size() >= len; });
  }

  void CheckMessage(const fuchsia::logger::LogMessage& msg, LogLevel severity,
                    const std::string& str, const std::vector<std::string>& tags) {
    EXPECT_EQ(msg.dropped_logs, 0u);
    EXPECT_EQ(msg.pid, 0u);
    EXPECT_EQ(msg.tid, 0u);
    EXPECT_NE(msg.time, 0u);
    EXPECT_EQ(msg.severity, static_cast<int32_t>(severity));
    EXPECT_EQ(msg.msg, str);
    EXPECT_EQ(msg.tags.size(), tags.size());
    for (const auto& t : tags) {
      EXPECT_NE(std::find(msg.tags.begin(), msg.tags.end(), t), msg.tags.end())
          << "Tag " << t << "  in message, but got tags " << fxl::JoinStrings(msg.tags, ",");
    }
  }

  void DestroyLogger() { logger_ = nullptr; }

  zx::socket& socket() { return sock_; }

 private:
  zx::socket sock_;
  std::shared_ptr<TestListener> listener_;
  std::unique_ptr<ManagedLogger> logger_;
};

TEST_F(ManagedLoggerTest, BreaksNewLines) {
  Startup(kLoggerName, false);
  Write("Hello\nWorld\nExtra");
  WaitForMessageCount(2);
  // no more than two messages should have been logged:
  EXPECT_EQ(messages().size(), 2u);
  CheckMessage(messages()[0], LogLevel::INFO, "Hello", {kLoggerName});
  CheckMessage(messages()[1], LogLevel::INFO, "World", {kLoggerName});

  // when we append the rest of the line it'll get logged:
  Write(" Data\n");
  WaitForMessageCount(3);
  CheckMessage(messages()[2], LogLevel::INFO, "Extra Data", {kLoggerName});
}

TEST_F(ManagedLoggerTest, LogLevelError) {
  Startup(kLoggerName, true);
  Write("Hello\n");
  WaitForMessageCount(1);

  CheckMessage(messages()[0], LogLevel::ERROR, "Hello", {kLoggerName});
}

TEST_F(ManagedLoggerTest, LargeMessageGetsBroken) {
  Startup(kLoggerName, false);
  std::string msg = "a";
  std::stringstream big_message;
  for (int i = 0; i < ManagedLogger::BufferSize - 1; i++) {
    msg[0] = 'a' + (i % 26);
    Write(msg);
    big_message << msg;
    if (i % 10 == 0) {
      // Make the listener consume the message bit by bit
      RunLoopUntilIdle();
    }
  }
  Write("More\n");
  WaitForMessageCount(2);
  CheckMessage(messages()[0], LogLevel::INFO, big_message.str(), {kLoggerName});
  CheckMessage(messages()[1], LogLevel::INFO, "More", {kLoggerName});
}

TEST_F(ManagedLoggerTest, DrainsSocketOnExit) {
  Startup(kLoggerName, false);
  constexpr char kMsg[] = "Hello World";
  constexpr char kShortMsg[] = "Hello";

  constexpr size_t kMessages = 25;
  for (size_t i = 0; i < kMessages; i++) {
    Write(std::string(kMsg) + "\n");
  }
  // Write one last incomplete message, missing a newline.
  Write(kShortMsg);
  DestroyLogger();
  ASSERT_EQ(messages().size(), kMessages + 1);
  for (auto i = messages().begin(); i != messages().end() - 1; i++) {
    CheckMessage(*i, LogLevel::INFO, kMsg, {kLoggerName});
  }
  CheckMessage(messages().back(), LogLevel::INFO, kShortMsg, {kLoggerName});
}

}  // namespace testing
}  // namespace netemul
