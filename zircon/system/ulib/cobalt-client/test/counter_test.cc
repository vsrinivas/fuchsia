// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/c/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <atomic>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/counter.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <unittest/unittest.h>

#include "fake_logger.h"

namespace cobalt_client {
namespace {

constexpr uint64_t kMetricId = 1;

// Number of threads spawned for multi-threaded tests.
constexpr uint64_t kThreads = 20;

// Component name.
constexpr char kComponent[] = "SomeRamdomCounterComponent";

constexpr uint32_t kEventCode = 2;

}  // namespace

namespace internal {
namespace {

MetricOptions MakeMetricOptions() {
  MetricOptions options;
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = kMetricId;
  options.component = kComponent;
  options.event_codes = {kEventCode, 0, 0, 0, 0};
  return options;
}

MetricInfo MakeMetricInfo() { return MetricInfo::From(MakeMetricOptions()); }

RemoteCounter MakeRemoteCounter() { return RemoteCounter(MakeMetricInfo()); }

// Verify that increments increases the underlying count by 1.
// This is veryfying the default behaviour.
bool TestIncrement() {
  BEGIN_TEST;
  BaseCounter<uint64_t> counter;

  ASSERT_EQ(counter.Load(), 0);
  counter.Increment();
  ASSERT_EQ(counter.Load(), 1);
  counter.Increment();
  ASSERT_EQ(counter.Load(), 2);
  END_TEST;
}

// Verify that increments increases the underlying count by val.
bool TestIncrementByVal() {
  BEGIN_TEST;
  BaseCounter<uint64_t> counter;

  ASSERT_EQ(counter.Load(), 0);
  counter.Increment(23);
  ASSERT_EQ(counter.Load(), 23);
  END_TEST;
}

// Verify that exchangest the underlying count to 0, and returns the current value.
// This is veryfying the default behaviour.
bool TestExchange() {
  BEGIN_TEST;
  BaseCounter<uint64_t> counter;

  counter.Increment(24);
  ASSERT_EQ(counter.Load(), 24);
  EXPECT_EQ(counter.Exchange(), 24);
  ASSERT_EQ(counter.Load(), 0);
  END_TEST;
}

// Verify that exchangest the underlying count to 0, and returns the current value.
// This is veryfying the default behaviour.
bool TestExchangeByVal() {
  BEGIN_TEST;
  BaseCounter<uint64_t> counter;

  counter.Increment(4);
  ASSERT_EQ(counter.Load(), 4);
  EXPECT_EQ(counter.Exchange(3), 4);
  ASSERT_EQ(counter.Load(), 3);
  EXPECT_EQ(counter.Exchange(2), 3);
  ASSERT_EQ(counter.Load(), 2);
  END_TEST;
}

struct IncrementArgs {
  // Counter to be operated on.
  BaseCounter<uint64_t>* counter;

  // Wait for main thread to signal before we start.
  sync_completion_t* start;

  // Amount to increment the counter with.
  BaseCounter<uint64_t>::Type value;
};

int IncrementFn(void* args) {
  IncrementArgs* increment_args = static_cast<IncrementArgs*>(args);
  sync_completion_wait(increment_args->start, zx::sec(20).get());
  for (uint64_t i = 0; i < increment_args->value; ++i) {
    increment_args->counter->Increment(increment_args->value);
  }
  return thrd_success;
}

bool TestIncrementMultiThread() {
  BEGIN_TEST;
  sync_completion_t start;
  BaseCounter<uint64_t> counter;
  fbl::Vector<thrd_t> thread_ids;
  IncrementArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].counter = &counter;
    args[i].value = static_cast<BaseCounter<uint64_t>::Type>(i + 1);
    args[i].start = &start;
    ASSERT_EQ(thrd_create(&thread_id, IncrementFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Each thread should increase the counter by a total of value^2, which yields a total of:
  // kThreads * (kThreads + 1) * (2 * kThreads + 1) / 6 = Sum(i=1, kThreads) i^2
  ASSERT_EQ(counter.Load(), kThreads * (kThreads + 1) * (2 * kThreads + 1) / 6);
  END_TEST;
}

struct ExchangeArgs {
  // Counter to be operated on.
  BaseCounter<uint64_t>* counter;

  // Accumulated value of exchanged values in the counter.
  std::atomic<BaseCounter<uint64_t>::Type>* accumulated;

  // Wait for main thread to signal before we start.
  sync_completion_t* start;

  // Amount to increment the counter with.
  BaseCounter<uint64_t>::Type value;
};

// After all threads exit, all but one value has been added to the accumulated var,
// this is the last thread to call exchange, which is why test should add the current
// value of the counter to the accumulated atomic obtain a deterministic result.
int ExchangeFn(void* args) {
  ExchangeArgs* exchange_args = static_cast<ExchangeArgs*>(args);
  sync_completion_wait(exchange_args->start, zx::sec(20).get());
  BaseCounter<uint64_t>::Type value = exchange_args->counter->Exchange(exchange_args->value);
  exchange_args->accumulated->fetch_add(value, std::memory_order_relaxed);
  return thrd_success;
}

// Verify that when exchanging all intermediate values are seen by exactly 1 thread.
// Everythread will exhange the seen value with their value, and add it to an atomic,
// the result should be the same as above except that we need to add counter.Load() +
// accumulated_value.
bool TestExchangeMultiThread() {
  BEGIN_TEST;
  sync_completion_t start;
  BaseCounter<uint64_t> counter;
  std::atomic<BaseCounter<uint64_t>::Type> accumulated(0);
  fbl::Vector<thrd_t> thread_ids;
  ExchangeArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].counter = &counter;
    args[i].value = static_cast<BaseCounter<uint64_t>::Type>(i + 1);
    args[i].start = &start;
    args[i].accumulated = &accumulated;
    ASSERT_EQ(thrd_create(&thread_id, ExchangeFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Each thread should increase the counter by a total of value, which yields a total of:
  // kThreads * (kThreads + 1)/ 2 = Sum(i=1, kThreads) i
  ASSERT_EQ(counter.Load() + accumulated.load(std::memory_order_relaxed),
            kThreads * (kThreads + 1) / 2);
  END_TEST;
}

// Verify flushed values match and the delta is set to 0.
bool TestFlush() {
  BEGIN_TEST;
  RemoteCounter counter = MakeRemoteCounter();
  counter.Increment(20);
  MetricInfo actual_metric_info;
  MetricInfo expected_metric_info = MakeMetricInfo();
  FakeLogger logger;
  logger.set_should_fail(false);
  int64_t actual_count;

  // Check that all data is present, we abuse some implementation details which guarantee
  // that metadata is first in the flushed values, and the last element is the event_data we
  // are measuring, which adds some restrictions to the internal implementation, but makes the
  // test cleaner and readable.
  ASSERT_TRUE(counter.Flush(&logger));

  actual_metric_info = logger.logged_counts()[0].metric_info;
  actual_count = logger.logged_counts()[0].count;

  // We capture the values and then verify outside to avoid having to pass flag around,
  // and have more descriptive messages on errors.
  ASSERT_TRUE(actual_metric_info == expected_metric_info);
  ASSERT_EQ(actual_count, 20);
  ASSERT_EQ(counter.Load(), 0);
  END_TEST;
}

// Verify that the metadata used to create the counter is part of the flushes observation
// and that the current value of the counter is correct, plus resets to 0 after flush.
bool TestUndoFlush() {
  BEGIN_TEST;
  RemoteCounter counter = MakeRemoteCounter();
  FakeLogger logger;
  logger.set_should_fail(false);

  counter.Increment(20);
  ASSERT_TRUE(counter.Flush(&logger));
  ASSERT_EQ(counter.Load(), 0);
  counter.UndoFlush();
  ASSERT_EQ(counter.Load(), 20);
  END_TEST;
}

struct FlushArgs {
  // Counter to be incremented or flushed by a given thread.
  RemoteCounter* counter;

  // Used to make the threads wait until all have been initialized.
  sync_completion_t* start;

  // Flushed accumulated value.
  std::atomic<RemoteCounter::Type>* accumulated;

  // Number of times to perform the operation.
  size_t operation_count = 0;

  // Flush operations are thread-compatible and we use a mutex
  // to provide a barrier. Interaction between other operations
  // and flushes should remain thread-safe and eventually consistent.
  fbl::Mutex* flush_mutex = nullptr;

  // Whether the thread should flush or increment.
  bool flush = false;
};

int FlushFn(void* args) {
  FlushArgs* flush_args = static_cast<FlushArgs*>(args);
  sync_completion_wait(flush_args->start, zx::sec(20).get());
  FakeLogger logger;
  for (size_t i = 0; i < flush_args->operation_count; ++i) {
    if (flush_args->flush) {
      fbl::AutoLock lock(flush_args->flush_mutex);
      flush_args->counter->Flush(&logger);
    } else {
      flush_args->counter->Increment();
    }
  }

  if (flush_args->flush) {
    for (auto count_entry : logger.logged_counts()) {
      flush_args->accumulated->fetch_add(count_entry.count, std::memory_order_relaxed);
    }
  }
  return thrd_success;
}

// Verify the consistency calling flush from multiple threads. There will be kThreads incrementing
// the counter, kThreads flushing, and at the end we flush again, and the accumulated counter should
// be equal to the total |kThreads| (|kThreads| + 1) / 2.
bool TestFlushMultithread() {
  BEGIN_TEST;
  sync_completion_t start;
  RemoteCounter counter = MakeRemoteCounter();
  std::atomic<BaseCounter<int64_t>::Type> accumulated(0);
  fbl::Vector<thrd_t> thread_ids;
  fbl::Mutex flush_mutex;
  FlushArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].counter = &counter;
    args[i].operation_count = static_cast<BaseCounter<uint64_t>::Type>(i + 1);
    args[i].start = &start;
    args[i].accumulated = &accumulated;
    args[i].flush = i % 2;
    args[i].flush_mutex = &flush_mutex;
    ASSERT_EQ(thrd_create(&thread_id, FlushFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // The total number of increment is the sum of odd numbers from 1 to 20 so
  // |ceil(kThreads/2)|^2.
  constexpr size_t ceil_threads = (kThreads / 2) + kThreads % 2;

  // Since the last thread to finish might not have flushed, we verify that the total of whats
  // left, plust what we have accumulated equals the expected amount.
  ASSERT_EQ(counter.Load() + accumulated.load(std::memory_order_relaxed),
            ceil_threads * ceil_threads);
  END_TEST;
}

BEGIN_TEST_CASE(BaseCounterTest)
RUN_TEST(TestIncrement)
RUN_TEST(TestIncrementByVal)
RUN_TEST(TestExchange)
RUN_TEST(TestExchangeByVal)
RUN_TEST(TestIncrementMultiThread)
RUN_TEST(TestExchangeMultiThread)
END_TEST_CASE(BaseCounterTest)

BEGIN_TEST_CASE(RemoteCounterTest)
RUN_TEST(TestFlush)
RUN_TEST(TestUndoFlush)
RUN_TEST(TestFlushMultithread)
END_TEST_CASE(RemoteCounterTest)

}  // namespace
}  // namespace internal

namespace {

bool TestIncrement() {
  BEGIN_TEST;
  Counter counter(internal::MakeMetricOptions());

  ASSERT_EQ(counter.GetRemoteCount(), 0);
  counter.Increment();
  ASSERT_EQ(counter.GetRemoteCount(), 1);
  counter.Increment(24);
  ASSERT_EQ(counter.GetRemoteCount(), 25);
  END_TEST;
}

struct IncrementArgs {
  // Counter to be incremented.
  Counter* counter;

  // Number of times to call increment.
  size_t times = 0;

  // Signals threads to start incrementing.
  sync_completion_t* start;
};

int IncrementFn(void* args) {
  IncrementArgs* increment_args = static_cast<IncrementArgs*>(args);
  sync_completion_wait(increment_args->start, zx::sec(20).get());

  for (size_t i = 0; i < increment_args->times; ++i) {
    increment_args->counter->Increment(increment_args->times);
  }
  return thrd_success;
}

bool TestIncrementMultiThread() {
  BEGIN_TEST;
  sync_completion_t start;
  Counter counter(internal::MakeMetricOptions());
  fbl::Vector<thrd_t> thread_ids;
  IncrementArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].counter = &counter;
    args[i].times = static_cast<Counter::Count>(i + 1);
    args[i].start = &start;
    ASSERT_EQ(thrd_create(&thread_id, IncrementFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Each thread should increase the counter by a total of value^2, which yields a total of:
  // kThreads * (kThreads + 1) * (2 * kThreads + 1) / 6 = Sum(i=1, kThreads) i^2
  ASSERT_EQ(counter.GetRemoteCount(), kThreads * (kThreads + 1) * (2 * kThreads + 1) / 6);
  END_TEST;
}

BEGIN_TEST_CASE(CounterTest)
RUN_TEST(TestIncrement)
RUN_TEST(TestIncrementMultiThread)
END_TEST_CASE(CounterTest)

}  // namespace
}  // namespace cobalt_client
