// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/system_log.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/async_test_clock.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr syslog::LogSeverity kLogInfo = syslog::LOG_INFO;
constexpr syslog::LogSeverity kLogWarning = syslog::LOG_WARNING;

std::string MessageJson(const int id) {
  return fxl::StringPrintf(
      R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["tag_%d"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Message %d"
        }
      }
    }
  }
]
)JSON",
      id, id);
}

std::vector<std::string> Messages() { return {MessageJson(1), MessageJson(2), MessageJson(3)}; }

class SystemLogTest : public UnitTestFixture {
 public:
  SystemLogTest()
      : executor_(dispatcher()),
        clock_(dispatcher()),
        system_log_(dispatcher(), services(), &clock_, &redactor_, kActivePeriod) {}

 protected:
  void SetUpLogServer(std::vector<std::string> messages) {
    log_server_ = std::make_unique<stubs::DiagnosticsArchive>(
        std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
            std::move(messages)));
    InjectServiceProvider(log_server_.get(), feedback_data::kArchiveAccessorName);
  }

  AttachmentValue CollectSystemLog(const zx::duration timeout = zx::sec(1)) {
    AttachmentValue result(Error::kNotSet);
    executor_.schedule_task(
        system_log_.Get(timeout)
            .and_then([&result](AttachmentValue& res) { result = std::move(res); })
            .or_else([] { FX_LOGS(FATAL) << "Bad path"; }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor& GetExecutor() { return executor_; }

  SystemLog& GetSystemLog() { return system_log_; }

  ::fpromise::promise<AttachmentValue> CollectSystemLog(const uint64_t ticket,
                                                        const zx::duration timeout = zx::sec(1)) {
    return system_log_.Get(ticket, timeout).or_else([]() -> ::fpromise::result<AttachmentValue> {
      FX_LOGS(FATAL) << "Bad path";
      return ::fpromise::error();
    });
  }

 protected:
  static constexpr auto kActivePeriod = zx::hour(1);
  static constexpr auto kLogTimestamp = zx::sec(1234);

  timekeeper::Clock* Clock() { return &clock_; }
  const stubs::DiagnosticsArchiveBase& LogServer() const { return *log_server_; }

 private:
  async::Executor executor_;
  timekeeper::AsyncTestClock clock_;
  IdentityRedactor redactor_{inspect::BoolProperty()};
  std::unique_ptr<stubs::DiagnosticsArchiveBase> log_server_;

  SystemLog system_log_;
};

TEST_F(SystemLogTest, GetTerminatesDueToLogTimestamp) {
  SetUpLogServer(Messages());

  const auto log = CollectSystemLog();
  EXPECT_FALSE(log.HasError());

  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(SystemLogTest, GetTerminatesDueToTimeout) {
  SetUpLogServer(Messages());

  // Prime the clock so log collection won't be completed due to message timestamps.
  RunLoopFor(kLogTimestamp + zx::sec(1));

  const auto log = CollectSystemLog(zx::min(1));
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kTimeout);

  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(SystemLogTest, GetTerminatesDueToTimeoutWithEmptyLog) {
  SetUpLogServer({});

  // Prime the clock so log collection won't be completed due to message timestamps.
  RunLoopFor(kLogTimestamp + zx::sec(1));

  const auto log = CollectSystemLog(zx::min(1));
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kTimeout);

  EXPECT_FALSE(log.HasValue());
}

TEST_F(SystemLogTest, GetTerminatesDueToForceCompletion) {
  const uint64_t kTicket = 1234;
  SetUpLogServer(Messages());

  // Prime the clock so log collection won't be completed due to message timestamps.
  RunLoopFor(kLogTimestamp + zx::sec(1));

  AttachmentValue log(Error::kNotSet);
  GetExecutor().schedule_task(CollectSystemLog(kTicket).and_then(
      [&log](AttachmentValue& result) { log = std::move(result); }));

  // Giving some time to actually collect some log data, so that system_log is not empty
  RunLoopUntilIdle();

  // Forcefully terminating log collection
  GetSystemLog().ForceCompletion(kTicket, Error::kDefault);

  RunLoopUntilIdle();
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kDefault);
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(SystemLogTest, ForceCompletionCalledAfterTermination) {
  const uint64_t kTicket = 1234;
  SetUpLogServer(Messages());

  AttachmentValue log(Error::kNotSet);
  GetExecutor().schedule_task(CollectSystemLog(kTicket).and_then(
      [&log](AttachmentValue& result) { log = std::move(result); }));

  RunLoopFor(zx::sec(1));

  GetSystemLog().ForceCompletion(kTicket, Error::kDefault);
  ASSERT_FALSE(log.HasError());

  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(SystemLogTest, GetTerminatesDueToLogTimestampWithEmptyLog) {
  SetUpLogServer({});

  const auto log = CollectSystemLog();
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kTimeout);

  EXPECT_FALSE(log.HasValue());
}

TEST_F(SystemLogTest, ActivePeriodExpires) {
  SetUpLogServer(Messages());

  auto log = CollectSystemLog();
  ASSERT_FALSE(log.HasError());

  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");

  // Become disconnected from the server after |kActivePeriod| expires.
  RunLoopFor(kActivePeriod);
  ASSERT_FALSE(LogServer().IsBound());

  log = CollectSystemLog();

  // Get empty logs because the original data was cleared and the server doesn't respond.
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kTimeout);

  // Ensure reconnection happened.
  EXPECT_TRUE(LogServer().IsBound());
}

TEST_F(SystemLogTest, ActivePeriodResets) {
  SetUpLogServer(Messages());

  auto log = CollectSystemLog(zx::min(1));
  ASSERT_FALSE(log.HasError());

  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");

  RunLoopFor(kActivePeriod / 2);
  ASSERT_TRUE(LogServer().IsBound());

  log = CollectSystemLog();

  // Expect a timeout because the stub isn't supposed to respond.
  ASSERT_TRUE(log.HasError());
  EXPECT_EQ(log.Error(), Error::kTimeout);

  // And the original data wasn't cleared.
  ASSERT_TRUE(log.HasValue());
  EXPECT_EQ(log.Value(), R"([01234.000][00200][00300][tag_1] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");

  // Become disconnected |kActivePeriod| after the last collection request completes.
  RunLoopFor(kActivePeriod / 2);
  ASSERT_TRUE(LogServer().IsBound());

  RunLoopFor(kActivePeriod / 2);
  ASSERT_FALSE(LogServer().IsBound());
}

TEST_F(SystemLogTest, GetCalledWithSameTicket) {
  const uint64_t kTicket = 1234;

  // Expect a crash because a ticket cannot be reused.
  ASSERT_DEATH(
      {
        const auto log1 = CollectSystemLog(kTicket);
        const auto log2 = CollectSystemLog(kTicket);
      },
      "Ticket used twice: ");
}

class SimpleRedactor : public RedactorBase {
 public:
  SimpleRedactor() : RedactorBase(inspect::BoolProperty()) {}

 private:
  std::string& Redact(std::string& text) override {
    if (text.find("ERRORS ERR") == text.npos && text.find("Offset") == text.npos) {
      text = "REDACTED";
    }
    return text;
  }

  std::string UnredactedCanary() const override { return ""; }
  std::string RedactedCanary() const override { return ""; }
};

feedback_data::LogSink::MessageOr ToMessage(const std::string& msg,
                                            syslog::LogSeverity severity = kLogInfo,
                                            std::vector<std::string> tags = {"tag1", "tag2"}) {
  return ::fpromise::ok(fuchsia::logger::LogMessage{
      .pid = 100,
      .tid = 101,
      .time = (zx::sec(1) + zx::msec(10)).get(),
      .severity = severity,
      .dropped_logs = 0,
      .tags = std::move(tags),
      .msg = msg,
  });
}

feedback_data::LogSink::MessageOr ToMessage(const std::string& msg, const zx::duration time) {
  return ::fpromise::ok(fuchsia::logger::LogMessage{
      .pid = 100,
      .tid = 101,
      .time = time.get(),
      .severity = kLogInfo,
      .dropped_logs = 0,
      .tags = {"tag1", "tag2"},
      .msg = msg,
  });
}

feedback_data::LogSink::MessageOr ToError(const std::string& error) {
  return ::fpromise::error(error);
}

TEST(LogBufferTest, SafeAfterInterruption) {
  IdentityRedactor redactor(inspect::BoolProperty{});
  LogBuffer buffer(StorageSize::Gigabytes(100), &redactor);
  ASSERT_TRUE(buffer.SafeAfterInterruption());
}

TEST(LogBufferTest, OrderingOnAdd) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Gigabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 0")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
)");

  // Should be deduplicated and before "log 1".
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(19))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
)");

  // Should be deduplicated and after "log 1".
  EXPECT_TRUE(buffer.Add(ToMessage("log 3", zx::sec(21))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 3", zx::sec(21))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  // Should be after "log 3".
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");

  // Should be before "log 3".
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(20))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00020.000][00100][00101][tag1, tag2] INFO: log 4
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");

  // Should be before "log 3", but not aggregated with other "log 4".
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00020.000][00100][00101][tag1, tag2] INFO: log 4
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 4
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");

  // Should be before "log 3".
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 2")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(22))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
[00020.000][00100][00101][tag1, tag2] INFO: log 4
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 4
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
!!! Failed to format chunk: ERRORS ERR 2 !!!
[00022.000][00100][00101][tag1, tag2] INFO: log 4
)");
}

TEST(LogBufferTest, OrderingOnEnforce) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  // 190 bytes is approximately enough to store 3 log messages.
  LogBuffer buffer(StorageSize::Bytes(190), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1", zx::sec(20))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"([00020.000][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  // Should be before "log 1".
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_EQ(buffer.ToString(), R"([00018.000][00100][00101][tag1, tag2] INFO: log 2
[00020.000][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  // Should be before "log 1" and not deduplicated against the earlier "log 2"
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(19))));

  EXPECT_EQ(buffer.ToString(), R"([00018.000][00100][00101][tag1, tag2] INFO: log 2
[00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  // Should be deduplicated and after "log 1".
  EXPECT_TRUE(buffer.Add(ToMessage("log 3", zx::sec(21))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 3", zx::sec(21))));

  EXPECT_EQ(buffer.ToString(), R"([00020.000][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");

  // Should be after "log 3".
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));

  EXPECT_EQ(buffer.ToString(), R"([00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");

  // Should be before "log 3".
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(20))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 4", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"([00020.000][00100][00101][tag1, tag2] INFO: log 4
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00021.000][00100][00101][tag1, tag2] INFO: log 3
!!! MESSAGE REPEATED 1 MORE TIME !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
)");
}

TEST(LogBufferTest, RepeatedMessage) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1")));

  // Exact same message, severity and tags: should be deduplicated
  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");
}

TEST(LogBufferTest, DoNotDeduplicateIfDifferentMessage) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2")));

  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: log 1
[00001.010][00100][00101][tag1, tag2] INFO: log 2
)");
}

TEST(LogBufferTest, DoNotDeduplicateIfDifferentSeverity) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1", kLogInfo)));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1", kLogWarning)));

  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: log 1
[00001.010][00100][00101][tag1, tag2] WARN: log 1
)");
}

TEST(LogBufferTest, DoNotDeduplicateIfDifferentTags) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1", kLogInfo, {"tag1", "tag2"})));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1", kLogInfo, {"tag1"})));

  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: log 1
[00001.010][00100][00101][tag1] INFO: log 1
)");
}

TEST(LogBufferTest, TimestampZeroOnFirstError) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 1 !!!
)");
}

TEST(LogBufferTest, RepeatedError) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 1 !!!
!!! MESSAGE REPEATED 1 MORE TIME !!!
)");
}

TEST(LogBufferTest, DoNotDeduplicateIfDifferentError) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 2")));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 1 !!!
!!! Failed to format chunk: ERRORS ERR 2 !!!
)");
}

TEST(LogBufferTest, RedactsLogs) {
  SimpleRedactor redactor;

  LogBuffer buffer(StorageSize::Megabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToMessage("log 1")));

  EXPECT_TRUE(buffer.Add(ToMessage("log 2")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2")));

  EXPECT_TRUE(buffer.Add(ToMessage("log 3")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 3")));

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 1")));

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 2")));
  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 2")));

  EXPECT_TRUE(buffer.Add(ToMessage("log 4")));

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 3")));

  EXPECT_TRUE(buffer.Add(ToMessage("log 4")));

  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: REDACTED
!!! MESSAGE REPEATED 5 MORE TIMES !!!
!!! Failed to format chunk: ERRORS ERR 1 !!!
!!! Failed to format chunk: ERRORS ERR 2 !!!
!!! MESSAGE REPEATED 1 MORE TIME !!!
[00001.010][00100][00101][tag1, tag2] INFO: REDACTED
!!! Failed to format chunk: ERRORS ERR 3 !!!
[00001.010][00100][00101][tag1, tag2] INFO: REDACTED
)");
}

TEST(LogBufferTest, NotifyInterruption) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Gigabytes(100), &redactor);

  EXPECT_TRUE(buffer.Add(ToError("ERRORS ERR 0")));
  EXPECT_TRUE(buffer.Add(ToMessage("log 1", zx::sec(20))));

  EXPECT_EQ(buffer.ToString(), R"(!!! Failed to format chunk: ERRORS ERR 0 !!!
[00020.000][00100][00101][tag1, tag2] INFO: log 1
)");

  // Should clear the buffer.
  buffer.NotifyInterruption();

  EXPECT_THAT(buffer.ToString(), IsEmpty());

  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(18))));
  EXPECT_TRUE(buffer.Add(ToMessage("log 2", zx::sec(19))));

  EXPECT_EQ(buffer.ToString(), R"([00018.000][00100][00101][tag1, tag2] INFO: log 2
!!! MESSAGE REPEATED 2 MORE TIMES !!!
)");
}

TEST(LogBufferTest, RunsActions) {
  IdentityRedactor redactor(inspect::BoolProperty{});

  LogBuffer buffer(StorageSize::Gigabytes(100), &redactor);

  bool run1{false};
  buffer.ExecuteAfter(zx::sec(0), [&run1] { run1 = true; });

  bool run2{false};
  buffer.ExecuteAfter(zx::sec(0), [&run2] { run2 = true; });

  bool run3{false};
  buffer.ExecuteAfter(zx::sec(5), [&run3] { run3 = true; });

  bool run4{false};
  buffer.ExecuteAfter(zx::sec(5), [&run4] { run4 = true; });

  bool run5{false};
  buffer.ExecuteAfter(zx::sec(7), [&run5] { run5 = true; });

  bool run6{false};
  buffer.ExecuteAfter(zx::sec(30), [&run6] { run6 = true; });

  buffer.Add(ToMessage("unused", zx::sec(0)));

  EXPECT_TRUE(run1);
  EXPECT_TRUE(run2);
  EXPECT_FALSE(run3);
  EXPECT_FALSE(run4);
  EXPECT_FALSE(run5);
  EXPECT_FALSE(run6);

  buffer.Add(ToMessage("unused", zx::sec(10)));

  EXPECT_TRUE(run1);
  EXPECT_TRUE(run2);
  EXPECT_TRUE(run3);
  EXPECT_TRUE(run4);
  EXPECT_TRUE(run5);
  EXPECT_FALSE(run6);

  buffer.NotifyInterruption();

  EXPECT_TRUE(run1);
  EXPECT_TRUE(run2);
  EXPECT_TRUE(run3);
  EXPECT_TRUE(run4);
  EXPECT_TRUE(run5);
  EXPECT_TRUE(run6);
}

}  // namespace
}  // namespace forensics::feedback
