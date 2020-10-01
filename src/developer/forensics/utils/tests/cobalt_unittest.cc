// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <limits>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace cobalt {
namespace {

constexpr uint32_t kMaxQueueSize = 500u;
constexpr CrashState kEventCode = CrashState::kFiled;
constexpr CrashState kAnotherEventCode = CrashState::kUploaded;
constexpr uint64_t kCount = 2u;
constexpr zx::duration kLoggerBackoffInitialDelay = zx::msec(100);

using fuchsia::cobalt::Status;
using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class CobaltTest : public UnitTestFixture {
 public:
  CobaltTest()
      : clock_(new timekeeper::TestClock()),
        cobalt_(std::make_unique<Logger>(dispatcher(), services(),
                                         std::unique_ptr<timekeeper::TestClock>(clock_))) {}

 protected:
  void LogOccurrence() {
    cobalt_->LogOccurrence(kEventCode);
    events_.emplace_back(kEventCode);
  }

  void LogCount() {
    cobalt_->LogCount(kEventCode, kCount);
    events_.emplace_back(kEventCode, kCount);
  }

  void LogMultidimensionalOccurrence() {
    cobalt_->LogOccurrence(kEventCode, kAnotherEventCode);
    events_.emplace_back(kEventCode, kAnotherEventCode);
  }

  const std::vector<Event> SentCobaltEvents() { return events_; }

  // The lifetime of |clock_| is managed by |cobalt_|.
  timekeeper::TestClock* clock_;
  std::unique_ptr<Logger> cobalt_;

 private:
  std::vector<Event> events_;
};

TEST_F(CobaltTest, Check_Log) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  for (size_t i = 0; i < 5; ++i) {
    LogCount();
    LogOccurrence();
    LogMultidimensionalOccurrence();
    RunLoopUntilIdle();
  }

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));
}

TEST_F(CobaltTest, Check_Timer) {
  constexpr zx::time kStartTime(0);
  constexpr zx::time kEndTime(kStartTime + zx::usec(5));

  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  clock_->Set(kStartTime);
  const uint64_t timer_id = cobalt_->StartTimer();

  clock_->Set(kEndTime);
  cobalt_->LogElapsedTime(SnapshotGenerationFlow::kSuccess, timer_id);

  RunLoopUntilIdle();

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  Event(SnapshotGenerationFlow::kSuccess, (kEndTime - kStartTime).to_usecs()),
              }));
}

TEST_F(CobaltTest, Check_LoggerLosesConnection_BeforeLoggingEvents) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  CloseLoggerConnection();

  for (size_t i = 0; i < 5; ++i) {
    LogOccurrence();
    EXPECT_FALSE(WasLogEventCalled());
  }
  RunLoopUntilIdle();

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));
}

TEST_F(CobaltTest, Check_LoggerLosesConnection_WhileLoggingEvents) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  for (size_t i = 0; i < 5; ++i) {
    LogOccurrence();
  }
  RunLoopUntilIdle();

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));

  CloseLoggerConnection();

  for (size_t i = 0; i < 5; ++i) {
    LogCount();
  }

  // Run the loop for twice the delay to account for the nondeterminism of
  // backoff::ExponentialBackoff.
  RunLoopFor(kLoggerBackoffInitialDelay * 2);

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));
}

TEST_F(CobaltTest, Check_LoggerDoesNotRespond_ClosesConnection) {
  auto stub_logger = std::make_unique<stubs::CobaltLoggerIgnoresFirstEvents>(5u);
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>(std::move(stub_logger)));

  for (size_t i = 0; i < 5; ++i) {
    LogOccurrence();
    RunLoopUntilIdle();
  }

  CloseLoggerConnection();

  LogOccurrence();

  // Run the loop for twice the delay to account for the nondeterminism of
  // backoff::ExponentialBackoff.
  RunLoopFor(kLoggerBackoffInitialDelay * 2);

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));
}

TEST_F(CobaltTest, Check_QueueReachesMaxSize) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  CloseLoggerConnection();

  std::vector<Event> events;
  for (size_t i = 0; i < kMaxQueueSize; ++i) {
    cobalt_->LogOccurrence(kEventCode);
    events.emplace_back(kEventCode);
  }

  for (size_t i = 0; i < kMaxQueueSize; ++i) {
    cobalt_->LogOccurrence(kEventCode);
  }
  RunLoopUntilIdle();

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(events));
}

TEST_F(CobaltTest, Check_ExponentialBackoff) {
  constexpr uint64_t num_attempts = 10u;
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactoryCreatesOnRetry>(num_attempts));
  CloseLoggerConnection();

  // We need to conservatively approximate the exponential backoff used by |logger_| so we don't
  // unintentionally run the loop for too long.
  zx::duration delay = kLoggerBackoffInitialDelay;
  uint32_t retry_factor = 2u;

  LogOccurrence();
  RunLoopUntilIdle();

  for (size_t i = 0; i < num_attempts - 1; ++i) {
    RunLoopFor(delay);
    EXPECT_FALSE(WasLogEventCalled());
    delay *= retry_factor;
  }
  RunLoopFor(delay);

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray(SentCobaltEvents()));
}

TEST_F(CobaltTest, Check_LoopOutlivesCobalt) {
  // We set up a scenario in which |cobalt_| has posted a task on the loop to reconnect to
  // fuchsia.cobalt/Logger and then is freed. This test should trigger ASAN if the task is not
  // cancelled.
  constexpr uint64_t num_attempts = 10u;
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactoryCreatesOnRetry>(num_attempts));
  CloseLoggerConnection();

  zx::duration delay = kLoggerBackoffInitialDelay;
  uint32_t retry_factor = 2u;

  LogOccurrence();
  RunLoopUntilIdle();
  for (size_t i = 0; i < num_attempts / 2; ++i) {
    RunLoopFor(delay);
    EXPECT_FALSE(WasLogEventCalled());
    delay *= retry_factor;
  }
  cobalt_.reset();
  RunLoopFor(delay);

  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(CobaltTest, SmokeTest_NoLoggerFactoryServer) {
  RunLoopUntilIdle();
  for (size_t i = 0; i < 5u; ++i) {
    LogOccurrence();
    RunLoopUntilIdle();
  }
}

}  // namespace
}  // namespace cobalt
}  // namespace forensics
