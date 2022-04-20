// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/log_source.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::feedback_data {
namespace {

using ::testing::ElementsAreArray;

constexpr zx::duration kTimestamp = zx::sec(1234);

LogSink::MessageOr BuildOutputLogMessage(const std::string& text) {
  return ::fpromise::ok(testing::BuildLogMessage(syslog::LOG_INFO, text, kTimestamp, {}));
}

std::string BuildInputLogMessage(const std::string& message) {
  constexpr char fmt[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": %zu,
      "severity": "INFO",
      "pid": 7559,
      "tid": 7687
    },
    "payload": {
      "root": {
      "message": {
        "value": "%s"
        }
      }
    }
  }
]
)JSON";

  return fxl::StringPrintf(fmt, kTimestamp.get(), message.c_str());
}

bool operator==(const std::vector<LogSink::MessageOr>& lhs,
                const std::vector<LogSink::MessageOr>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].is_ok() != rhs[i].is_ok()) {
      return false;
    }

    if (lhs[i].is_error()) {
      return lhs[i].error() == rhs[i].error();
    }
    const auto& l = lhs[i].value();
    const auto& r = rhs[i].value();

    if (!(l.pid == r.pid) && (l.tid == r.tid) && (l.time == r.time) && (l.severity == r.severity) &&
        (l.dropped_logs == r.dropped_logs) && (l.tags == r.tags) && (l.msg == r.msg)) {
      return false;
    }
  }

  return true;
}

class MonotonicBackoff : public backoff::Backoff {
 public:
  zx::duration GetNext() override { return zx::sec(seconds_++); }

  void Reset() override { seconds_ = 1; }

 private:
  size_t seconds_{1u};
};

class SimpleLogSink : public LogSink {
 public:
  SimpleLogSink(bool safe_after_interruption = false)
      : safe_after_interruption_(safe_after_interruption) {}

  bool Add(LogSink::MessageOr message) override {
    messages_.push_back(std::move(message));
    return true;
  }

  const std::vector<LogSink::MessageOr>& Messages() const { return messages_; }

  void NotifyInterruption() override {
    was_interrupted_ = true;
    if (safe_after_interruption_) {
      messages_.clear();
    }
  }

  bool SafeAfterInterruption() const override { return safe_after_interruption_; }

  bool WasInterrupted() const { return was_interrupted_; }

 private:
  std::vector<LogSink::MessageOr> messages_;
  bool safe_after_interruption_;
  bool was_interrupted_{false};
};

using LogSourceTest = UnitTestFixture;

TEST_F(LogSourceTest, WritesToSink) {
  constexpr zx::duration kTimeWaitForLimitedLogs = zx::sec(60);
  const zx::duration kArchivePeriod = zx::msec(750);

  SimpleLogSink sink;
  LogSource source(dispatcher(), services(), &sink, std::make_unique<MonotonicBackoff>());

  const std::vector<std::vector<std::string>> batches({
      {
          BuildInputLogMessage("line 0"),
          BuildInputLogMessage("line 1"),
          BuildInputLogMessage("line 2"),
          BuildInputLogMessage("line 3"),

      },
      {
          BuildInputLogMessage("line 4"),
          BuildInputLogMessage("line 5"),
          BuildInputLogMessage("line 6"),
          BuildInputLogMessage("line 7"),
      },
      {BuildInputLogMessage("line 8")},
      {BuildInputLogMessage("line 9")},
      {BuildInputLogMessage("line A")},
      {BuildInputLogMessage("line B")},
      {BuildInputLogMessage("line C")},
      {BuildInputLogMessage("line D")},
      {},
  });

  stubs::DiagnosticsArchive archive(std::make_unique<stubs::DiagnosticsBatchIteratorDelayedBatches>(
      dispatcher(), batches, kTimeWaitForLimitedLogs, kArchivePeriod));

  InjectServiceProvider(&archive, "fuchsia.diagnostics.FeedbackArchiveAccessor");

  source.Start();
  RunLoopFor(kTimeWaitForLimitedLogs);

  std::vector<LogSink::MessageOr> expected;
  for (const auto& batch : batches) {
    // Break to prevent |source| from fetching more messages.
    if (batch.empty()) {
      break;
    }

    for (const auto& msg : batch) {
      expected.push_back(BuildOutputLogMessage(msg));
    }
    EXPECT_TRUE(sink.Messages() == expected);
    RunLoopFor(kArchivePeriod);
  }

  EXPECT_TRUE(archive.IsBound());
  source.Stop();

  RunLoopUntilIdle();
  EXPECT_FALSE(archive.IsBound());
}

TEST_F(LogSourceTest, NotifyInterruptionArchive) {
  SimpleLogSink sink;
  LogSource source(dispatcher(), services(), &sink, std::make_unique<MonotonicBackoff>());

  stubs::DiagnosticsArchiveClosesArchiveConnection archive;

  InjectServiceProvider(&archive, "fuchsia.diagnostics.FeedbackArchiveAccessor");

  source.Start();
  RunLoopUntilIdle();

  EXPECT_TRUE(sink.WasInterrupted());
}

TEST_F(LogSourceTest, NotifyInterruptionIterator) {
  SimpleLogSink sink;
  LogSource source(dispatcher(), services(), &sink, std::make_unique<MonotonicBackoff>());

  stubs::DiagnosticsArchiveClosesIteratorConnection archive;

  InjectServiceProvider(&archive, "fuchsia.diagnostics.FeedbackArchiveAccessor");

  source.Start();
  RunLoopUntilIdle();

  EXPECT_TRUE(sink.WasInterrupted());
}

TEST_F(LogSourceTest, ReconnectsOnSafeAfterInterruption) {
  SimpleLogSink sink(/*safe_after_interruption=*/true);
  LogSource source(dispatcher(), services(), &sink, std::make_unique<MonotonicBackoff>());

  stubs::DiagnosticsArchiveClosesFirstIteratorConnection archive(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverResponds>());

  InjectServiceProvider(&archive, "fuchsia.diagnostics.FeedbackArchiveAccessor");

  source.Start();
  RunLoopUntilIdle();

  EXPECT_TRUE(sink.WasInterrupted());
  EXPECT_FALSE(archive.IsBound());

  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(archive.IsBound());
}

TEST_F(LogSourceTest, DoesNotReconnectsOnNotSafeAfterInterruption) {
  SimpleLogSink sink(/*safe_after_interruption=*/false);
  LogSource source(dispatcher(), services(), &sink, std::make_unique<MonotonicBackoff>());

  stubs::DiagnosticsArchiveClosesFirstIteratorConnection archive(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverResponds>());

  InjectServiceProvider(&archive, "fuchsia.diagnostics.FeedbackArchiveAccessor");

  source.Start();
  RunLoopUntilIdle();

  EXPECT_TRUE(sink.WasInterrupted());
  EXPECT_FALSE(archive.IsBound());

  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(archive.IsBound());
}

}  // namespace
}  // namespace forensics::feedback_data
