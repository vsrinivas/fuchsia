// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/system_log.h"

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

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/redact/redactor.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr char kMessage1Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["tag_1", "tag_a"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Message 1"
        }
      }
    }
  }
]
)JSON";

constexpr char kMessage2Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["tag_2"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Message 2"
        }
      }
    }
  }
]
)JSON";

constexpr char kMessage3Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["tag_3"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Message 3"
        }
      }
    }
  }
]
)JSON";

class CollectLogDataTest : public UnitTestFixture {
 public:
  CollectLogDataTest() : executor_(dispatcher()) {}

 protected:
  void SetupLogServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    log_server_ = std::move(server);
    if (log_server_) {
      InjectServiceProvider(log_server_.get(), kArchiveAccessorName);
    }
  }

  ::fpromise::result<AttachmentValue> CollectSystemLog(const zx::duration timeout = zx::sec(1)) {
    return CollectSystemLog(&redactor_, timeout);
  }

  ::fpromise::result<AttachmentValue> CollectSystemLog(RedactorBase* redactor,
                                                       const zx::duration timeout = zx::sec(1)) {
    ::fpromise::result<AttachmentValue> result;
    executor_.schedule_task(feedback_data::CollectSystemLog(dispatcher(), services(),
                                                            fit::Timeout(timeout, /*action=*/[] {}),
                                                            redactor)
                                .then([&result](::fpromise::result<AttachmentValue>& res) {
                                  result = std::move(res);
                                }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::DiagnosticsArchiveBase> log_server_;
  IdentityRedactor redactor_{inspect::BoolProperty()};
};

TEST_F(CollectLogDataTest, Succeed_AllSystemLogs) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {kMessage1Json, kMessage2Json},
          {kMessage3Json},
          {},
      }))));

  ::fpromise::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(CollectLogDataTest, Succeed_PartialSystemLogs) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({kMessage1Json, kMessage2Json}))));

  ::fpromise::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kPartial);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
)");
  EXPECT_EQ(logs.Error(), Error::kTimeout);
}

TEST_F(CollectLogDataTest, Succeed_FormattingErrors) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {kMessage1Json, kMessage2Json},
          {kMessage3Json},
          {"foo", "bar"},
          {},
      }))));

  ::fpromise::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
!!! Failed to format chunk: Failed to parse content as JSON. Offset 1: Invalid value. !!!
!!! Failed to format chunk: Failed to parse content as JSON. Offset 0: Invalid value. !!!
)");
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

TEST_F(CollectLogDataTest, Succeed_AppliesRedaction) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {kMessage1Json, kMessage2Json},
          {kMessage3Json},
          {"foo", "bar"},
          {},
      }))));

  SimpleRedactor redactor;
  ::fpromise::result<AttachmentValue> result = CollectSystemLog(&redactor);
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: REDACTED
!!! MESSAGE REPEATED 2 MORE TIMES !!!
!!! Failed to format chunk: Failed to parse content as JSON. Offset 1: Invalid value. !!!
!!! Failed to format chunk: Failed to parse content as JSON. Offset 0: Invalid value. !!!
)");
}

LogSink::MessageOr ToMessage(const std::string& msg) {
  return ::fpromise::ok(fuchsia::logger::LogMessage{
      .pid = 100,
      .tid = 101,
      .time = (zx::sec(1) + zx::msec(10)).get(),
      .severity = syslog::LOG_INFO,
      .dropped_logs = 0,
      .tags = {"tag1", "tag2"},
      .msg = msg,
  });
}

LogSink::MessageOr ToMessage(const std::string& msg, const zx::duration time) {
  return ::fpromise::ok(fuchsia::logger::LogMessage{
      .pid = 100,
      .tid = 101,
      .time = time.get(),
      .severity = syslog::LOG_INFO,
      .dropped_logs = 0,
      .tags = {"tag1", "tag2"},
      .msg = msg,
  });
}

LogSink::MessageOr ToError(const std::string& error) { return ::fpromise::error(error); }

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

  EXPECT_EQ(buffer.ToString(), R"([00001.010][00100][00101][tag1, tag2] INFO: log 1
!!! MESSAGE REPEATED 1 MORE TIME !!!
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

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
