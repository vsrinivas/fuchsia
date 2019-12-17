// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/cobalt.h"

#include <lib/zx/time.h>

#include <limits>
#include <memory>
#include <vector>

#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr uint32_t kMaxQueueSize = 500u;
constexpr uint32_t kMetricIdStart = 1u;
constexpr uint32_t kEventCodeStart = std::numeric_limits<uint32_t>::max();

using fuchsia::cobalt::Status;

class CobaltTest : public UnitTestFixture {
 public:
  void SetUp() override { cobalt_ = std::make_unique<Cobalt>(services()); }

 protected:
  void SetUpCobaltLoggerFactory(std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory) {
    logger_factory_ = std::move(logger_factory);
    if (logger_factory_) {
      InjectServiceProvider(logger_factory_.get());
    }

    // Because |cobalt_| cannot send any messages until after the callback for creating the
    // Logger has executed, we must run the loop before attempting to log events, else |cobalt_|
    // will deem the Logger not ready.
    RunLoopUntilIdle();
  }

  const CobaltEvent& LastEvent() { return logger_factory_->LastEvent(); }

  bool WasLogEventCalled() { return logger_factory_->WasLogEventCalled(); }
  bool WasLogEventCountCalled() { return logger_factory_->WasLogEventCountCalled(); }

  void CloseAllConnections() { logger_factory_->CloseAllConnections(); }
  void CloseFactoryConnection() { logger_factory_->CloseFactoryConnection(); }
  void CloseLoggerConnection() { logger_factory_->CloseLoggerConnection(); }

  // We want generate new a new metric id and a new event code each time so we can guarantee that
  // the stub logger's values are changing.
  uint32_t NextMetricId() { return next_metric_id_++; }
  uint32_t NextEventCode() { return next_event_code_--; }

  std::unique_ptr<Cobalt> cobalt_;

 private:
  std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory_;

  // Define |next_metric_id| and |next_event_code| such that it's highly  unlikely that they'll ever
  // share the same value. Additionally, select starting values that are not the default constructed
  // value of | uint32_t | (which is 0).
  uint32_t next_metric_id_ = kMetricIdStart;
  uint32_t next_event_code_ = kEventCodeStart;
};

TEST_F(CobaltTest, Check_Log) {
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  uint32_t metric_id = 0;
  uint32_t event_code = 0;

  for (size_t i = 0; i < 5; ++i) {
    metric_id = NextMetricId();
    event_code = NextEventCode();

    cobalt_->LogOccurrence(metric_id, event_code);
    RunLoopUntilIdle();
    EXPECT_EQ(LastEvent(), CobaltEvent(CobaltEvent::Type::Occurrence, metric_id, event_code));
  }
}

TEST_F(CobaltTest, Check_LogCount) {
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  uint32_t metric_id = 0;
  uint32_t event_code = 0;

  for (size_t i = 0; i < 5; ++i) {
    metric_id = NextMetricId();
    event_code = NextEventCode();

    cobalt_->LogCount(metric_id, event_code, i);
    RunLoopUntilIdle();
    EXPECT_EQ(LastEvent(), CobaltEvent(CobaltEvent::Type::Count, metric_id, event_code, i));
  }
}

TEST_F(CobaltTest, Check_CallbackExecutes) {
  SetUpCobaltLoggerFactory(
      std::make_unique<StubCobaltLoggerFactory>(std::make_unique<StubCobaltLoggerFailsLogEvent>()));

  Status log_event_status = Status::OK;
  uint32_t metric_id = NextMetricId();
  uint32_t event_code = NextEventCode();

  cobalt_->LogOccurrence(metric_id, event_code,
                         [&log_event_status](Status status) { log_event_status = status; });
  RunLoopUntilIdle();
  EXPECT_EQ(log_event_status, Status::INVALID_ARGUMENTS);
}

TEST_F(CobaltTest, Check_LoggerLosesConnection) {
  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());

  uint32_t metric_id = 0;
  uint32_t event_code = 0;

  for (size_t i = 0; i < 5; ++i) {
    metric_id = NextMetricId();
    event_code = NextEventCode();

    cobalt_->LogOccurrence(metric_id, event_code);
    RunLoopUntilIdle();
    EXPECT_EQ(LastEvent(), CobaltEvent(CobaltEvent::Type::Occurrence, metric_id, event_code));
  }

  CloseLoggerConnection();
  RunLoopUntilIdle();

  // Attempt to log more, but the values should not be stored by the Logger.
  for (size_t i = 0; i < 5; ++i) {
    cobalt_->LogOccurrence(NextMetricId(), NextEventCode());
    RunLoopUntilIdle();
    // The stub is stuck on the last value before we closed the connection.
    EXPECT_EQ(LastEvent(), CobaltEvent(CobaltEvent::Type::Occurrence, metric_id, event_code));
  }
}

TEST_F(CobaltTest, Check_QueueReachesMaxSize) {
  // We setup this test so that |cobalt_| will queue up events to Log() until a minute has passed.
  // At that point class serving fuchsia.cobalt.Logger is deemed ready and |cobalt_| will flush the
  // queue.
  const zx::duration delay(zx::min(1));

  SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactoryDelaysCallback>(
      std::make_unique<StubCobaltLogger>(), dispatcher(), delay));

  uint32_t metric_id = 0;
  uint32_t event_code = 0;

  for (size_t i = 0; i < kMaxQueueSize; ++i) {
    metric_id = NextMetricId();
    event_code = NextEventCode();

    cobalt_->LogOccurrence(metric_id, event_code);
    RunLoopUntilIdle();
    ASSERT_FALSE(WasLogEventCalled());
  }

  RunLoopFor(delay);
  EXPECT_EQ(LastEvent(), CobaltEvent(CobaltEvent::Type::Occurrence, metric_id, event_code));
}

}  // namespace
}  // namespace feedback
