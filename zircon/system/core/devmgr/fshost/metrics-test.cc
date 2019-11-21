// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics.h"

#include <lib/async-testing/test_loop.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in-memory-logger.h>
#include <fs/metrics/events.h>
#include <zxtest/zxtest.h>

namespace devmgr {
namespace {

using EventIntType = std::underlying_type<fs_metrics::Event>::type;

std::unique_ptr<cobalt_client::Collector> MakeCollector(cobalt_client::InMemoryLogger** logger) {
  std::unique_ptr<cobalt_client::InMemoryLogger> logger_ptr =
      std::make_unique<cobalt_client::InMemoryLogger>();
  *logger = logger_ptr.get();
  return std::make_unique<cobalt_client::Collector>(std::move(logger_ptr));
}

class MetricsTest : public zxtest::Test {
 public:
  void SetUp() override { collector_ = MakeCollector(&logger_); }
  void TearDown() override { logger_ = nullptr; }

 protected:
  cobalt_client::InMemoryLogger* logger_;
  std::unique_ptr<cobalt_client::Collector> collector_;
};

cobalt_client::MetricOptions MakeMetricOptionsFromId(uint32_t metric_id) {
  cobalt_client::MetricOptions info = {};
  info.metric_id = metric_id;
  return info;
}

constexpr auto kCorruptionMetricId = static_cast<EventIntType>(fs_metrics::Event::kDataCorruption);

TEST_F(MetricsTest, LogMinfsDataCorruption) {
  FsHostMetrics metrics(std::move(collector_));
  ASSERT_EQ(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());
  metrics.LogMinfsCorruption();
  // Nothing is logged until flushed.
  ASSERT_EQ(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());

  // Once we flush, we should see the logged event in the metrics.
  metrics.mutable_collector()->Flush();
  ASSERT_NE(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());

  EXPECT_EQ(logger_->counters().at(MakeMetricOptionsFromId(kCorruptionMetricId)), 1);
}

TEST_F(MetricsTest, FlushUntilSuccessWorks) {
  async::TestLoop loop;
  FsHostMetrics metrics(std::move(collector_));
  metrics.LogMinfsCorruption();

  // Logger is not working
  logger_->fail_logging(true);
  metrics.FlushUntilSuccess(loop.dispatcher());
  loop.RunFor(zx::sec(10));

  // After 10 seconds, nothing should be logged.
  ASSERT_EQ(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());
  loop.RunFor(zx::sec(10));

  // After 20 seconds, nothing should be logged.
  ASSERT_EQ(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());

  // Logger begins working.
  logger_->fail_logging(false);

  // Work should complete now.
  loop.RunFor(zx::sec(10));

  ASSERT_NE(logger_->counters().find(MakeMetricOptionsFromId(kCorruptionMetricId)),
            logger_->counters().end());
  EXPECT_EQ(logger_->counters().at(MakeMetricOptionsFromId(kCorruptionMetricId)), 1);
}

}  // namespace
}  // namespace devmgr
