// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/in-memory-logger.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace {
using HistogramBucket = InMemoryLogger::HistogramBucket;

constexpr uint64_t kMetricId = 44;
constexpr uint64_t kMetric2Id = 45;
constexpr int64_t kCount = 32;

TEST(InMemoryLoggerTest, LogCounterOnce) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;
  InMemoryLogger logger;

  ASSERT_TRUE(logger.Log(metric_info, kCount));

  ASSERT_NE(logger.counters().find(metric_info), logger.counters().end(),
            "Failed to persist count.");
  EXPECT_EQ(logger.counters().at(metric_info), kCount);
}

TEST(InMemoryLoggerTest, LogMultipleCounters) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;
  MetricOptions metric_info_2;
  metric_info_2.metric_id = kMetric2Id;
  InMemoryLogger logger;

  ASSERT_TRUE(logger.Log(metric_info, kCount));
  ASSERT_TRUE(logger.Log(metric_info_2, kCount * 2));
  ASSERT_NE(logger.counters().find(metric_info), logger.counters().end(),
            "Failed to persist count.");
  ASSERT_NE(logger.counters().find(metric_info_2), logger.counters().end(),
            "Failed to persist count.");

  EXPECT_EQ(logger.counters().at(metric_info), kCount);
  EXPECT_EQ(logger.counters().at(metric_info_2), 2 * kCount);
}

TEST(InMemoryLoggerTest, LogCounterMultipleTimesAccumulates) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;
  InMemoryLogger logger;
  constexpr int64_t kCount = 25;

  ASSERT_TRUE(logger.Log(metric_info, kCount));
  ASSERT_TRUE(logger.Log(metric_info, kCount));
  ASSERT_TRUE(logger.Log(metric_info, kCount));

  ASSERT_NE(logger.counters().find(metric_info), logger.counters().end(),
            "Failed to persist count.");
  EXPECT_EQ(logger.counters().at(metric_info), kCount * 3);
}

constexpr HistogramBucket kHistBuckets[] = {
    {.index = 0, .count = 1},
    {.index = 2, .count = 5},
};

constexpr HistogramBucket kHistBuckets2[] = {
    {.index = 1, .count = 2},
    {.index = 5, .count = 3},
};

TEST(InMemoryLoggerTest, LogHistogramOnce) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;
  InMemoryLogger logger;

  ASSERT_TRUE(logger.Log(metric_info, kHistBuckets, fbl::count_of(kHistBuckets)));

  ASSERT_NE(logger.histograms().find(metric_info), logger.histograms().end(),
            "Failed to persist count.");
  const auto& histograms = logger.histograms().at(metric_info);
  ASSERT_EQ(histograms.size(), fbl::count_of(kHistBuckets));
  for (const auto bucket : kHistBuckets) {
    EXPECT_EQ(histograms.at(bucket.index), static_cast<Histogram<1>::Count>(bucket.count));
  }
}

TEST(InMemoryLoggerTest, LogMultipleHistograms) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;

  MetricOptions metric_info_2;
  metric_info_2.metric_id = kMetric2Id;
  InMemoryLogger logger;

  ASSERT_TRUE(logger.Log(metric_info, kHistBuckets, fbl::count_of(kHistBuckets)));
  ASSERT_TRUE(logger.Log(metric_info_2, kHistBuckets2, fbl::count_of(kHistBuckets2)));

  ASSERT_NE(logger.histograms().find(metric_info), logger.histograms().end(),
            "Failed to persist count.");
  ASSERT_NE(logger.histograms().find(metric_info_2), logger.histograms().end(),
            "Failed to persist count.");
  ASSERT_EQ(logger.histograms().size(), 2);
  {
    const auto& histograms = logger.histograms().at(metric_info);
    ASSERT_EQ(histograms.size(), fbl::count_of(kHistBuckets));
    for (const auto bucket : kHistBuckets) {
      EXPECT_EQ(histograms.at(bucket.index), static_cast<Histogram<1>::Count>(bucket.count));
    }
  }

  {
    const auto& histograms = logger.histograms().at(metric_info_2);
    ASSERT_EQ(histograms.size(), fbl::count_of(kHistBuckets));
    for (const auto bucket : kHistBuckets2) {
      EXPECT_EQ(histograms.at(bucket.index), static_cast<Histogram<1>::Count>(bucket.count));
    }
  }
}

TEST(InMemoryLoggerTest, LogHistogramMultipleTimesAccumulates) {
  MetricOptions metric_info;
  metric_info.metric_id = kMetricId;
  InMemoryLogger logger;

  ASSERT_TRUE(logger.Log(metric_info, kHistBuckets, fbl::count_of(kHistBuckets)));
  ASSERT_TRUE(logger.Log(metric_info, kHistBuckets, fbl::count_of(kHistBuckets)));
  ASSERT_TRUE(logger.Log(metric_info, kHistBuckets, fbl::count_of(kHistBuckets)));

  ASSERT_NE(logger.histograms().find(metric_info), logger.histograms().end(),
            "Failed to persist count.");

  const auto& histograms = logger.histograms().at(metric_info);
  ASSERT_EQ(histograms.size(), fbl::count_of(kHistBuckets));
  for (const auto bucket : kHistBuckets) {
    EXPECT_EQ(histograms.at(bucket.index), 3 * static_cast<Histogram<1>::Count>(bucket.count));
  }
}
}  // namespace
}  // namespace cobalt_client
