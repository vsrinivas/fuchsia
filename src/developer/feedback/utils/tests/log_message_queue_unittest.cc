// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/log_message_queue.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/zx/time.h>

#include <thread>
#include <vector>

#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::logger::LogMessage;

bool Eq(const fuchsia::logger::LogMessage& lhs, const fuchsia::logger::LogMessage& rhs) {
  bool pid = lhs.pid == rhs.pid;
  bool tid = lhs.tid == rhs.tid;
  bool time = lhs.time == rhs.time;
  bool severity = lhs.severity == rhs.severity;
  bool tags = lhs.tags == rhs.tags;
  bool msg = lhs.msg == rhs.msg;

  return pid && tid && time && severity && tags && msg;
}

LogMessage NewLogMessage() {
  static size_t next_pid_ = 0;

  return LogMessage{
      .pid = next_pid_++,
      .tid = 102,
      .time = zx::sec(103).to_secs(),
      .severity = FX_LOG_INFO,
      .tags = {"tag"},
      .msg = "i am a log message",
  };
}

TEST(LogMessageQueueTest, Check_SingleThreaded) {
  const size_t kCapacity = 32u;

  std::vector<LogMessage> pushed_log_messages;
  std::vector<LogMessage> popped_log_messages;

  LogMessageQueue message_queue(kCapacity);

  for (size_t i = 0; i < kCapacity; ++i) {
    const LogMessage msg = NewLogMessage();
    message_queue.Push(msg);
    pushed_log_messages.push_back(msg);
  }

  for (size_t i = 0; i < kCapacity; ++i) {
    popped_log_messages.push_back(message_queue.Pop());
  }

  ASSERT_EQ(popped_log_messages.size(), kCapacity);
  ASSERT_EQ(pushed_log_messages.size(), kCapacity);
  for (size_t i = 0; i < kCapacity; ++i) {
    EXPECT_TRUE(Eq(popped_log_messages[i], pushed_log_messages[i]));
  }
}

TEST(LogMessageQueueTest, Check_MessagesAreDropped) {
  const size_t kCapacity = 32u;

  std::vector<LogMessage> pushed_log_messages;
  std::vector<LogMessage> popped_log_messages;

  LogMessageQueue message_queue(kCapacity);

  for (size_t i = 0; i < kCapacity; ++i) {
    const LogMessage msg = NewLogMessage();
    message_queue.Push(msg);
    pushed_log_messages.push_back(msg);
  }

  for (size_t i = 0; i < kCapacity; ++i) {
    const LogMessage msg = NewLogMessage();
    message_queue.Push(msg);
  }

  for (size_t i = 0; i < kCapacity; ++i) {
    popped_log_messages.push_back(message_queue.Pop());
  }

  ASSERT_EQ(popped_log_messages.size(), kCapacity);
  ASSERT_EQ(pushed_log_messages.size(), kCapacity);
  for (size_t i = 0; i < kCapacity; ++i) {
    EXPECT_TRUE(Eq(popped_log_messages[i], pushed_log_messages[i]));
  }
}

TEST(LogMessageQueueTest, Check_Multithreaded) {
  const size_t kCapacity = 256;

  std::vector<LogMessage> pushed_log_messages;
  std::vector<LogMessage> popped_log_messages;

  LogMessageQueue message_queue(kCapacity);

  auto produce_messages = [&] {
    for (size_t i = 0; i < kCapacity; ++i) {
      const LogMessage msg = NewLogMessage();
      message_queue.Push(msg);
      pushed_log_messages.push_back(msg);
    }
  };

  auto consume_messages = [&] {
    while (popped_log_messages.size() < kCapacity) {
      popped_log_messages.push_back(message_queue.Pop());
    }
  };

  std::thread consumer(consume_messages);
  std::thread producer(produce_messages);

  consumer.join();
  producer.join();

  ASSERT_EQ(popped_log_messages.size(), kCapacity);
  ASSERT_EQ(pushed_log_messages.size(), kCapacity);
  for (size_t i = 0; i < kCapacity; ++i) {
    EXPECT_TRUE(Eq(popped_log_messages[i], pushed_log_messages[i]));
  }
}

}  // namespace
}  // namespace feedback
