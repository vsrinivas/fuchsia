// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <thread>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/in-memory-logger.h>
#include <cobalt-client/cpp/metric-options.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace internal {
namespace {

constexpr uint32_t kBucketCount = 20;

using TestBaseHistogram = BaseHistogram<kBucketCount>;

TEST(BaseHistogramTest, BucketCountStartsAtZero) {
  TestBaseHistogram histogram;

  ASSERT_EQ(kBucketCount, histogram.size());

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    EXPECT_EQ(0, histogram.GetCount(i));
  }
}

TEST(BaseHistogramTest, IncrementCountByDefaultIncrementsBucketCountByOne) {
  constexpr uint32_t kTargetBucket = 2;
  TestBaseHistogram histogram;

  histogram.IncrementCount(kTargetBucket);

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    if (i == kTargetBucket) {
      continue;
    }
    EXPECT_EQ(0, histogram.GetCount(i));
  }
  EXPECT_EQ(1, histogram.GetCount(kTargetBucket));
}

TEST(BaseHistogramTest, IncrementCountWithValueIncrementsBucketCountByValue) {
  constexpr uint32_t kTargetBucket = 2;
  constexpr uint64_t kValue = 123456;
  TestBaseHistogram histogram;

  histogram.IncrementCount(kTargetBucket, kValue);

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    if (i == kTargetBucket) {
      continue;
    }
    EXPECT_EQ(0, histogram.GetCount(i));
  }
  EXPECT_EQ(kValue, histogram.GetCount(kTargetBucket));
}

TEST(BaseHistogramTest, IncrementCountIsIsolated) {
  constexpr std::array<uint32_t, 5> kTargetBuckets = {2, 4, 6, 8, 10};
  constexpr uint64_t kValue = 123456;
  TestBaseHistogram histogram;

  for (auto bucket : kTargetBuckets) {
    histogram.IncrementCount(bucket, kValue + bucket);
  }

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    if (std::find(kTargetBuckets.begin(), kTargetBuckets.end(), i) != kTargetBuckets.end()) {
      EXPECT_EQ(kValue + i, histogram.GetCount(i));
      continue;
    }
    EXPECT_EQ(0, histogram.GetCount(i));
  }
}

TEST(BaseHistogramTest, IncrementCountFromMultipleTheadsIsConsistent) {
  constexpr uint64_t kThreadCount = 20;
  constexpr uint64_t kTimes = 200;
  TestBaseHistogram histogram;
  std::array<std::thread, kThreadCount> incrementing_threads;
  sync_completion_t start_signal;

  auto increment_fn = [&histogram, &start_signal]() {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    for (uint64_t i = 0; i < kTimes; ++i) {
      for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
        histogram.IncrementCount(bucket_index);
      }
    }
  };

  for (uint64_t i = 0; i < kThreadCount; i++) {
    incrementing_threads[i] = std::thread(increment_fn);
  }

  sync_completion_signal(&start_signal);

  for (auto& thread : incrementing_threads) {
    thread.join();
  }

  constexpr uint64_t kExpectedCount = kTimes * kThreadCount;
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(kExpectedCount, histogram.GetCount(bucket_index));
  }
}

using TestRemoteHistogram = RemoteHistogram<kBucketCount>;

// Default id for the histogram.
constexpr uint64_t kMetricId = 1;

// Default component name.
constexpr std::string_view kComponentName = "RemoteHisotgramComponentName";

// Default event codes.
constexpr std::array<uint32_t, MetricOptions::kMaxEventCodes> kEventCodes = {1, 2, 3, 4, 5};

HistogramOptions MakeHistogramOptions() {
  HistogramOptions options = HistogramOptions::CustomizedExponential(kBucketCount, 2, 1, 0);
  options.metric_id = kMetricId;
  options.component = kComponentName;
  options.event_codes = kEventCodes;
  return options;
}

TestRemoteHistogram MakeRemoteHistogram() { return TestRemoteHistogram(MakeHistogramOptions()); }

TEST(RemoteHistogramTest, FlushSetsBucketsToZeroAndReturnsTrueIfLogSucceeds) {
  TestRemoteHistogram histogram = MakeRemoteHistogram();
  InMemoryLogger logger;
  logger.fail_logging(false);

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    histogram.IncrementCount(bucket_index, bucket_index + 1);
  }

  ASSERT_TRUE(histogram.Flush(&logger));

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(0, histogram.GetCount(bucket_index));
  }

  const auto& logged_histograms = logger.histograms();
  auto logged_histogram_itr = logged_histograms.find(histogram.metric_options());
  ASSERT_NE(logged_histograms.end(), logged_histogram_itr);

  const auto& logged_histogram = logged_histogram_itr->second;
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    const uint64_t kExpectedCount = bucket_index + 1;
    ASSERT_NE(logged_histogram.end(), logged_histogram.find(bucket_index));
    EXPECT_EQ(kExpectedCount, logged_histogram.at(bucket_index));
  }
}

TEST(RemoteHistogramTest, FlushSetsBucketsToZeroAndReturnsFalseIfLogFails) {
  TestRemoteHistogram histogram = MakeRemoteHistogram();
  InMemoryLogger logger;
  logger.fail_logging(true);

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    histogram.IncrementCount(bucket_index, bucket_index + 1);
  }

  ASSERT_FALSE(histogram.Flush(&logger));

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(0, histogram.GetCount(bucket_index));
  }

  const auto& logged_histograms = logger.histograms();
  auto logged_histogram_itr = logged_histograms.find(histogram.metric_options());
  ASSERT_EQ(logged_histograms.end(), logged_histogram_itr);
}

TEST(RemoteHistogramTest, UndoFlushSetsCounterToPreviousValue) {
  TestRemoteHistogram histogram = MakeRemoteHistogram();
  InMemoryLogger logger;
  logger.fail_logging(true);

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    histogram.IncrementCount(bucket_index, bucket_index + 1);
  }

  ASSERT_FALSE(histogram.Flush(&logger));
  histogram.UndoFlush();

  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(bucket_index + 1, histogram.GetCount(bucket_index));
  }
}

using TestHistogram = Histogram<kBucketCount>;

TEST(HistogramTest, ConstructFromOptionsIsOk) {
  ASSERT_NO_DEATH([] { [[maybe_unused]] TestHistogram histogram(MakeHistogramOptions()); });
}

TEST(HistogramTest, ConstructFromOptionsWithCollectorIsOk) {
  ASSERT_NO_DEATH([] {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    [[maybe_unused]] TestHistogram histogram(MakeHistogramOptions(), &collector);
  });
}

TEST(HistogramTest, InitilizeAlreadyInitializedHistogramIsAssertionError) {
  ASSERT_DEATH([]() {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    [[maybe_unused]] TestHistogram histogram(MakeHistogramOptions(), &collector);
    histogram.Initialize(MakeHistogramOptions(), &collector);
  });
}

void InMemoryLoggerContainsHistogramWithBucketCount(const TestHistogram& histogram,
                                                    const InMemoryLogger& logger,
                                                    int64_t logged_value, uint64_t logged_count) {
  const auto& logged_histograms = logger.histograms();
  auto logged_histogram_itr = logged_histograms.find(histogram.GetOptions());
  ASSERT_NE(logged_histograms.end(), logged_histogram_itr);

  const auto& logged_histogram = logged_histogram_itr->second;
  uint32_t bucket_index = histogram.GetOptions().map_fn(static_cast<double>(logged_value),
                                                        histogram.size(), histogram.GetOptions());

  auto bucket_itr = logged_histogram.find(bucket_index);
  ASSERT_NE(logged_histogram.end(), bucket_itr);

  uint64_t actual_count = bucket_itr->second;
  EXPECT_EQ(logged_count, actual_count);
}

fit::function<void(uint64_t, uint64_t)> MakeLoggedHistogramContainsChecker(
    const TestHistogram& histogram, const InMemoryLogger& logger) {
  return [&histogram, &logger](uint64_t value, uint64_t count) {
    InMemoryLoggerContainsHistogramWithBucketCount(histogram, logger, value, count);
  };
}

TEST(HistogramTest, AddIncreasesCorrectBucketCount) {
  constexpr uint64_t kValue = 25;
  std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
  logger->fail_logging(false);
  auto* logger_ptr = logger.get();

  Collector collector(std::move(logger));
  TestHistogram histogram(MakeHistogramOptions(), &collector);

  histogram.Add(kValue);

  ASSERT_EQ(1, histogram.GetCount(kValue));
  ASSERT_TRUE(collector.Flush());
  ASSERT_EQ(0, histogram.GetCount(kValue));

  auto logged_histogram_contains = MakeLoggedHistogramContainsChecker(histogram, *logger_ptr);
  ASSERT_NO_FAILURES(logged_histogram_contains(kValue, 1));
}

TEST(HistogramTest, AddWithCountIncreasesCorrectBucketCount) {
  constexpr uint64_t kValue = 25;
  constexpr uint64_t kCount = 25678;
  std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
  logger->fail_logging(false);
  auto* logger_ptr = logger.get();

  Collector collector(std::move(logger));
  TestHistogram histogram(MakeHistogramOptions(), &collector);

  histogram.Add(kValue, kCount);

  ASSERT_EQ(kCount, histogram.GetCount(kValue));
  ASSERT_TRUE(collector.Flush());
  ASSERT_EQ(0, histogram.GetCount(kValue));

  auto logged_histogram_contains = MakeLoggedHistogramContainsChecker(histogram, *logger_ptr);
  ASSERT_NO_FAILURES(logged_histogram_contains(kValue, kCount));
}

TEST(HistogramTest, AddIncreasesCountByOne) {
  constexpr uint64_t kValue = 25;
  TestHistogram histogram(MakeHistogramOptions());

  histogram.Add(kValue);

  EXPECT_EQ(1, histogram.GetCount(kValue));
}

TEST(HistogramTest, AddValueIncreasesCountByValue) {
  constexpr uint64_t kValue = 25;
  constexpr uint64_t kTimes = 100;

  TestHistogram histogram(MakeHistogramOptions());

  histogram.Add(kValue, kTimes);

  EXPECT_EQ(kTimes, histogram.GetCount(kValue));
}

TEST(HistogramTest, AddOnMultipleThreadsWithSynchronizedFlushingIsConsistent) {
  constexpr uint64_t kTimes = 1;
  constexpr uint64_t kThreadCount = 20;
  std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
  auto* logger_ptr = logger.get();
  std::mutex logger_mutex;
  Collector collector(std::move(logger));

  TestHistogram histogram(MakeHistogramOptions(), &collector);
  sync_completion_t start_signal;
  std::array<std::thread, kThreadCount> spamming_threads = {};

  auto get_value_for_bucket = [&histogram](uint32_t bucket_index) {
    return static_cast<int64_t>(histogram.GetOptions().reverse_map_fn(
        bucket_index, histogram.size(), histogram.GetOptions()));
  };

  auto increment_fn = [&histogram, &start_signal, &get_value_for_bucket]() {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    for (uint64_t i = 0; i < kTimes; ++i) {
      for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
        const int64_t kValue = get_value_for_bucket(bucket_index);
        histogram.Add(kValue, bucket_index + 1);
      }
    }
  };

  auto flushing_fn = [&collector, &logger_ptr, &logger_mutex, &start_signal]() {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    for (uint64_t i = 0; i < kTimes; ++i) {
      bool result = collector.Flush();
      {
        std::lock_guard lock(logger_mutex);
        logger_ptr->fail_logging(!result);
      }
    }
  };

  for (uint64_t thread = 0; thread < kThreadCount; ++thread) {
    if (thread % 2 == 0) {
      spamming_threads[thread] = std::thread(increment_fn);
    } else {
      spamming_threads[thread] = std::thread(flushing_fn);
    }
  }

  sync_completion_signal(&start_signal);

  for (auto& thread : spamming_threads) {
    thread.join();
  }
  logger_ptr->fail_logging(false);
  ASSERT_TRUE(collector.Flush());

  constexpr uint64_t kBaseExpectedCount = (kThreadCount / 2) * kTimes;
  auto logged_histogram_contains = MakeLoggedHistogramContainsChecker(histogram, *logger_ptr);
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    const uint64_t kExpectedCount = kBaseExpectedCount * (bucket_index + 1);
    const int64_t kValue = get_value_for_bucket(bucket_index);
    ASSERT_NO_FAILURES(logged_histogram_contains(kValue, kExpectedCount));
  }
}

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
