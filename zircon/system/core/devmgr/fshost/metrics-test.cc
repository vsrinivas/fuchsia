// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics.h"

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

TEST_F(MetricsTest, LogMinfsDataCorruption) {
  FsHostMetrics metrics(std::move(collector_));
  ASSERT_EQ(logger_->counters().find(static_cast<EventIntType>(fs_metrics::Event::kDataCorruption)),
            logger_->counters().end());
  metrics.LogMinfsCorruption();
  // Nothing is logged until flushed.
  ASSERT_EQ(logger_->counters().find(static_cast<EventIntType>(fs_metrics::Event::kDataCorruption)),
            logger_->counters().end());

  // Once we flush, we should see the logged event in the metrics.
  metrics.mutable_collector()->Flush();
  ASSERT_NE(logger_->counters().find(static_cast<EventIntType>(fs_metrics::Event::kDataCorruption)),
            logger_->counters().end());

  EXPECT_EQ(logger_->counters().at(static_cast<EventIntType>(fs_metrics::Event::kDataCorruption)),
            1);
}

}  // namespace
}  // namespace devmgr
