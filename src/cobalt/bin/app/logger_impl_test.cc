// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_impl.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "third_party/cobalt/src/logger/fake_logger.h"

namespace cobalt {

class LoggerImplTest : public ::testing::Test {
 public:
  LoggerImplTest()
      : fake_logger_(new logger::testing::FakeLogger),
        logger_impl_(std::unique_ptr<logger::LoggerInterface>(fake_logger_), nullptr),
        logger_(&logger_impl_) {}

 protected:
  logger::testing::FakeLogger* fake_logger_;
  LoggerImpl logger_impl_;
  fuchsia::cobalt::Logger* logger_;
};

TEST_F(LoggerImplTest, PauseDuringBatch) {
  std::vector<fuchsia::cobalt::CobaltEvent> events;
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  logger_->LogCobaltEvents(std::move(events), [](Status status) {});
  std::map<logger::PerProjectLoggerCallsMadeMetricDimensionLoggerMethod, uint32_t>
      internal_logger_calls = fake_logger_->internal_logger_calls();
  // Only the LogCobaltEvents call should be recorded.
  EXPECT_EQ(internal_logger_calls.size(), 1);
  EXPECT_EQ(
      internal_logger_calls[logger::LoggerCallsMadeMetricDimensionLoggerMethod::LogCobaltEvents],
      1);
}

// Tests that if StartTimer() and EndTimer() are invoked when the LoggerImpl
// was constructed without a TimerManager, then instead of crashing we
// return an error.
TEST_F(LoggerImplTest, NoTimerPresent) {
  Status status = Status::OK;
  logger_->StartTimer(0u, 0u, "", "", 0u, 0u, [&status](Status s) { status = s; });
  EXPECT_EQ(status, Status::INTERNAL_ERROR);
  status = Status::OK;
  logger_->EndTimer("", 0u, 0u, [&status](Status s) { status = s; });
  EXPECT_EQ(status, Status::INTERNAL_ERROR);
}

}  // namespace cobalt
