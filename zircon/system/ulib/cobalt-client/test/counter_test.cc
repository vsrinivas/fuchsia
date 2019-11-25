// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/counter.h>

#include <lib/sync/completion.h>
#include <stdint.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <thread>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/in-memory-logger.h>
#include <cobalt-client/cpp/metric-options.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace {

using internal::RemoteCounter;

using TestCounter = internal::BaseCounter<int64_t>;

// Default |MetricOption| values.
constexpr uint64_t kMetricId = 1;
constexpr std::string_view kComponentName = "TestCounter";
constexpr std::array<uint32_t, MetricOptions::kMaxEventCodes> kEventCodes = {0, 1, 2, 3, 4};

// Returns a set of options with the default values.
MetricOptions MakeMetricOptions() {
  MetricOptions options = {};
  options.metric_id = kMetricId;
  options.component = kComponentName;
  options.event_codes = kEventCodes;
  return options;
}

RemoteCounter MakeRemoteCounter() { return RemoteCounter(MakeMetricOptions()); }

TEST(BaseCounterTest, OnCreateCountIsZero) {
  TestCounter counter = {};

  EXPECT_EQ(0, counter.Load());
}

TEST(BaseCounterTest, IncrementByDefaultIncreasesCountByOne) {
  TestCounter counter = {};

  ASSERT_EQ(0, counter.Load());

  counter.Increment();

  EXPECT_EQ(1, counter.Load());
}

TEST(BaseCounterTest, IncrementByValueIncreasesCountByValue) {
  constexpr int64_t kValue = -25;
  TestCounter counter = {};

  ASSERT_EQ(0, counter.Load());

  counter.Increment(kValue);

  EXPECT_EQ(kValue, counter.Load());
}

TEST(BaseCounterTest, IncrementRepeateadlyAccumulateCorrectly) {
  TestCounter counter = {};

  ASSERT_EQ(0, counter.Load());

  for (int64_t i = 0; i < 10; ++i) {
    counter.Increment(i);
  }
  constexpr int64_t kExpected = 9 * (9 + 1) / 2;
  EXPECT_EQ(kExpected, counter.Load());

  counter.Increment();
  EXPECT_EQ(kExpected + 1, counter.Load());
}

TEST(BaseCounterTest, ExchangeByDefaultSetsToZero) {
  constexpr int64_t kValue = -1;
  TestCounter counter = {};

  ASSERT_EQ(0, counter.Load());
  counter.Increment(kValue);

  EXPECT_EQ(kValue, counter.Exchange());
  EXPECT_EQ(0, counter.Load());
}

TEST(BaseCounterTest, ExchangeByValueSetsToValue) {
  constexpr int64_t kValue = -1;
  TestCounter counter = {};

  ASSERT_EQ(0, counter.Load());
  counter.Increment(kValue);

  constexpr int64_t kExpectedValue = -1234556;
  EXPECT_EQ(kValue, counter.Exchange(kExpectedValue));
  EXPECT_EQ(kExpectedValue, counter.Load());
}

TEST(BaseCounterTest, IncrementByMultipleThreadsIsEventuallyConsistent) {
  constexpr uint64_t kThreadCount = 20;
  constexpr uint64_t kTimes = 10;
  TestCounter counter = {};
  std::array<std::thread, kThreadCount> spamming_threads = {};
  sync_completion_t start_signal = {};

  auto increment_fn = [&counter, &start_signal](uint64_t times, int64_t value) {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    for (uint64_t i = 0; i < times; ++i) {
      counter.Increment(value);
    }
  };

  for (uint64_t i = 0; i < kThreadCount; ++i) {
    spamming_threads[i] = std::thread(increment_fn, kTimes, static_cast<int64_t>(i));
  }

  sync_completion_signal(&start_signal);

  for (auto& thread : spamming_threads) {
    thread.join();
  }
  // Sum from (0, kN) = Sum from (0, kThreadCount - 1)
  constexpr uint64_t kN = kThreadCount - 1;
  // Each thread does |kTimes| calls, meaning |kTimes| * |Sum(0, kN)|.
  constexpr int64_t kExpectedCount = kTimes * (kN * (kN + 1)) / 2;

  EXPECT_EQ(kExpectedCount, counter.Load());
}

TEST(BaseCounterTest, ExchangeByMultipleThreadsIsConsistent) {
  constexpr uint64_t kTimes = 100;
  constexpr uint64_t kThreadCount = 20;
  constexpr uint64_t kValueCount = 20;

  TestCounter counter = {};
  std::array<std::atomic<uint64_t>, kValueCount> seen_values = {0};
  std::array<std::thread, kThreadCount> spamming_threads = {};
  sync_completion_t start_signal = {};

  auto exchange_fn = [&seen_values, &counter, &start_signal](uint64_t times, uint64_t index) {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    const uint64_t kTotalTimes = times + index;
    for (uint64_t i = 0; i < kTotalTimes; ++i) {
      auto previous = counter.Exchange(index);
      seen_values[previous]++;
    }
  };

  for (uint64_t i = 0; i < kThreadCount; ++i) {
    spamming_threads[i] = std::thread(exchange_fn, kTimes, static_cast<int64_t>(i));
  }

  sync_completion_signal(&start_signal);

  for (auto& thread : spamming_threads) {
    thread.join();
  }
  // One last exchange which enforces that everything but 0-index has kTimes, and 0 index has kTimes
  // + 1.
  exchange_fn(1, 0);
  ASSERT_EQ(0, counter.Load());

  for (uint64_t i = 1; i < kValueCount; ++i) {
    EXPECT_EQ(kTimes + i, seen_values[i].load());
  }
  EXPECT_EQ(kTimes + 1, seen_values[0].load());
}

TEST(RemoteCounterTest, FlushSetsCounterToZeroAndReturnsTrueIfLogSucceeds) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteCounter counter = MakeRemoteCounter();
  logger.fail_logging(false);

  ASSERT_EQ(0, counter.Load());
  counter.Increment(kValue);

  ASSERT_TRUE(counter.Flush(&logger));
  ASSERT_NE(logger.counters().end(), logger.counters().find(counter.metric_options()));

  EXPECT_EQ(0, counter.Load());
  EXPECT_EQ(kValue, logger.counters().at(counter.metric_options()));
}

TEST(RemoteCounterTest, FlushSetsCounterToZeroAndReturnsFalseIfLogSucceeds) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteCounter counter = MakeRemoteCounter();
  logger.fail_logging(true);

  ASSERT_EQ(0, counter.Load());
  counter.Increment(kValue);

  ASSERT_FALSE(counter.Flush(&logger));
  ASSERT_EQ(logger.counters().end(), logger.counters().find(counter.metric_options()));

  // Still resets itself.
  EXPECT_EQ(0, counter.Load());
}

TEST(RemoteCounterTest, UndoFlushSetsCounterToPreviousValue) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteCounter counter = MakeRemoteCounter();
  logger.fail_logging(true);

  ASSERT_EQ(0, counter.Load());
  counter.Increment(kValue);

  ASSERT_FALSE(counter.Flush(&logger));
  counter.UndoFlush();

  EXPECT_EQ(kValue, counter.Load());
}

TEST(CounterTest, ConstructFromOptionsIsOk) {
  ASSERT_NO_DEATH([] { [[maybe_unused]] Counter counter(MakeMetricOptions()); });
}

TEST(CounterTest, ConstructFromOptionsWithCollectorIsOk) {
  ASSERT_NO_DEATH([] {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    [[maybe_unused]] Counter counter(MakeMetricOptions(), &collector);
  });
}

TEST(CounterTest, InitializeWithOptionsAndCollectorIsOk) {
  ASSERT_NO_DEATH([]() {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    Counter counter;
    counter.Initialize(MakeMetricOptions(), &collector);
  });
}

TEST(CounterTest, InitilizeAlreadyInitializedCounterIsAssertionError) {
  ASSERT_DEATH([]() {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    Counter counter(MakeMetricOptions(), &collector);
    counter.Initialize(MakeMetricOptions(), &collector);
  });
}

TEST(CounterTest, IncrementIncreasesCountByOne) {
  Counter counter(MakeMetricOptions());

  counter.Increment();

  EXPECT_EQ(1, counter.GetCount());
}

TEST(CounterTest, IncrementByValueIncreasesCountByValue) {
  constexpr uint64_t kValue = -20;
  Counter counter(MakeMetricOptions());

  counter.Increment(kValue);

  EXPECT_EQ(kValue, counter.GetCount());
}

TEST(CounterTest, IncrementOnMultipleThreadsWithSynchronizedFlushingIsConsistent) {
  constexpr uint64_t kTimes = 500;
  constexpr uint64_t kThreadCount = 20;
  std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
  auto* logger_ptr = logger.get();
  std::mutex logger_mutex;
  Collector collector(std::move(logger));
  Counter counter(MakeMetricOptions(), &collector);
  sync_completion_t start_signal;
  std::array<std::thread, kThreadCount> spamming_threads = {};

  auto increment_fn = [&counter, &start_signal]() {
    sync_completion_wait(&start_signal, zx::duration::infinite().get());
    for (uint64_t i = 0; i < kTimes; ++i) {
      counter.Increment();
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

  constexpr uint64_t kExpectedCount = (kThreadCount / 2) * kTimes;
  ASSERT_NE(logger_ptr->counters().end(), logger_ptr->counters().find(counter.GetOptions()));
  EXPECT_EQ(kExpectedCount, logger_ptr->counters().at(counter.GetOptions()));
}

}  // namespace
}  // namespace cobalt_client
