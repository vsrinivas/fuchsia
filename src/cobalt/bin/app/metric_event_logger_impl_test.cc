// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/metric_event_logger_impl.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/lib/cobalt/cpp/metric_event_builder.h"
#include "third_party/cobalt/src/logger/fake_logger.h"

namespace cobalt {

class MetricEventLoggerImplTest : public ::testing::Test {
 public:
  MetricEventLoggerImplTest()
      : fake_logger_(new logger::testing::FakeLogger),
        logger_impl_(std::unique_ptr<logger::LoggerInterface>(fake_logger_)),
        logger_(&logger_impl_) {}

 protected:
  logger::testing::FakeLogger* fake_logger_;
  MetricEventLoggerImpl logger_impl_;
  fuchsia::cobalt::MetricEventLogger* logger_;
};

TEST_F(MetricEventLoggerImplTest, PauseDuringBatch) {
  std::vector<fuchsia::cobalt::MetricEvent> events;
  events.push_back(MetricEventBuilder(1).as_occurrence(1));
  events.push_back(MetricEventBuilder(1).as_occurrence(2));
  events.push_back(MetricEventBuilder(1).as_occurrence(3));
  events.push_back(MetricEventBuilder(1).as_occurrence(4));
  events.push_back(MetricEventBuilder(1).as_occurrence(5));
  logger_->LogMetricEvents(std::move(events), [](fuchsia::cobalt::Status status) {});
  std::map<logger::PerProjectLoggerCallsMadeMetricDimensionLoggerMethod, uint32_t>
      internal_logger_calls = fake_logger_->internal_logger_calls();
  // Only the LogCobaltEvents call should be recorded.
  EXPECT_EQ(internal_logger_calls.size(), 1);
  EXPECT_EQ(
      internal_logger_calls[logger::LoggerCallsMadeMetricDimensionLoggerMethod::LogMetricEvents],
      1);
}

}  // namespace cobalt
