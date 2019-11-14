// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/histogram.h>

#include <float.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <unittest/unittest.h>

#include "fake_logger.h"

namespace cobalt_client {
namespace internal {
namespace {

// Number of threads for running multi threaded tests.
constexpr uint64_t kThreads = 20;

// Number of buckets used for histogram(CUT).
constexpr uint32_t kBuckets = 40;

// Default id for the histogram.
constexpr uint64_t kMetricId = 1;

// Component name.
constexpr char kComponent[] = "SomeRandomHistogramComponent";

constexpr std::array<uint32_t, MetricInfo::kMaxEventCodes> kEventCodes = {2, 3, 4, 5, 6};

MetricInfo MakeMetricInfo() {
  MetricInfo metric_info;
  metric_info.metric_id = kMetricId;
  metric_info.component = kComponent;
  metric_info.event_codes = kEventCodes;
  return metric_info;
}

HistogramOptions MakeHistogramOptions() {
  HistogramOptions options = HistogramOptions::CustomizedExponential(kBuckets, 2, 1, 0);
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = kMetricId;
  options.component = kComponent;
  options.event_codes = kEventCodes;
  return options;
}

RemoteHistogram<kBuckets> MakeRemoteHistogram() {
  return RemoteHistogram<kBuckets>(MakeMetricInfo());
}

bool HistEventValuesEq(const fbl::Vector<HistogramBucket>& actual,
                       const fbl::Vector<HistogramBucket>& expected) {
  BEGIN_HELPER;
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    const HistogramBucket& actual_bucket = actual[i];
    bool found = false;
    for (size_t j = 0; j < expected.size(); ++j) {
      const HistogramBucket& expected_bucket = expected[j];
      if (actual_bucket.index != expected_bucket.index) {
        continue;
      }
      EXPECT_EQ(actual_bucket.count, expected_bucket.count);
      found = true;
      break;
    }
    ASSERT_TRUE(found);
  }
  END_HELPER;
}

// Verify the count of the appropiate bucket is updated on increment.
bool TestIncrement() {
  BEGIN_TEST;
  BaseHistogram<kBuckets> histogram;

  // Increase the count of each bucket bucket_index times.
  for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), 0);
    for (uint32_t times = 0; times < bucket_index; ++times) {
      histogram.IncrementCount(bucket_index);
    }
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
  }

  // Verify that the operations are isolated, each bucket should have bucket_index counts.
  for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
  }
  END_TEST;
}

// Verify the count of the appropiate bucket is updated on increment with a specified value. This
// verifies the behaviour for weighted histograms, where the weight is limited to an integer.
bool TestIncrementByVal() {
  BEGIN_TEST;
  BaseHistogram<kBuckets> histogram;

  // Increase the count of each bucket bucket_index times.
  for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), 0);
    histogram.IncrementCount(bucket_index, bucket_index);
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
  }

  // Verify that the operations are isolated, each bucket should have bucket_index counts.
  for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
  }
  END_TEST;
}

struct IncrementArgs {
  // Target histogram.
  BaseHistogram<kBuckets>* histogram;

  // Used for signaling the worker thread to start incrementing.
  sync_completion_t* start;

  // Number of times to call Increment.
  size_t operations = 0;
};

// Increment each bucket by 2 * operations * bucket_index.
int IncrementFn(void* args) {
  IncrementArgs* increment_args = static_cast<IncrementArgs*>(args);
  sync_completion_wait(increment_args->start, zx::sec(20).get());

  for (uint32_t bucket = 0; bucket < kBuckets; ++bucket) {
    for (size_t i = 0; i < increment_args->operations; ++i) {
      increment_args->histogram->IncrementCount(bucket, bucket);
    }
    increment_args->histogram->IncrementCount(bucket, bucket * increment_args->operations);
  }

  return thrd_success;
}

// Verifies that calling increment from multiple threads, yields consistent results. Multiple
// threads will call Increment a known number of times, then the total count per bucket should be
// the sum of the times each thread called Increment one each bucket.
bool TestIncrementMultiThread() {
  BEGIN_TEST;
  sync_completion_t start;
  BaseHistogram<kBuckets> histogram;
  fbl::Vector<thrd_t> thread_ids;
  IncrementArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].histogram = &histogram;
    args[i].operations = i;
    args[i].start = &start;
    ASSERT_EQ(thrd_create(&thread_id, IncrementFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Each thread increses each bucket by 2 * bucket_index, so the expected amount for
  // each bucket is: 2 * bucket_index * Sum(i=0, kThreads -1) i = 2 * bucket_index *
  // kThreads * (kThreads - 1) / 2;
  constexpr size_t amount = (kThreads - 1) * (kThreads);
  for (uint32_t i = 0; i < kBuckets; ++i) {
    // We take the sum of the accumulated and what is left, because the last
    // increment may have been scheduled after the last flush.
    EXPECT_EQ(histogram.GetCount(i), i * amount);
  }
  END_TEST;
}

bool TestLazyInitialization() {
  BEGIN_TEST;
  FakeLogger logger;
  RemoteHistogram<kBuckets> histogram;
  // Lazily initialize the options.
  histogram.Initialize(MakeHistogramOptions());

  fbl::Vector<HistogramBucket> expected_buckets;
  expected_buckets.reserve(histogram.size());

  // Increase the count of each bucket bucket_index times.
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), 0);
    histogram.IncrementCount(bucket_index, bucket_index);
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
    expected_buckets.push_back(
        {.index = static_cast<uint32_t>(bucket_index), .count = bucket_index});
    ASSERT_EQ(expected_buckets[bucket_index].index, bucket_index);
    ASSERT_EQ(expected_buckets[bucket_index].count, bucket_index);
  }

  ASSERT_TRUE(histogram.Flush(&logger));

  ASSERT_EQ(logger.logged_histograms().size(), 1);

  // Check that flushed data is actually what we expect:
  // The metadata is the same, and each bucket contains bucket_index count.
  auto& hist_entry = logger.logged_histograms()[0];

  EXPECT_TRUE(hist_entry.metric_info == MakeMetricInfo());

  // Verify there is a bucket event_data.
  EXPECT_TRUE(HistEventValuesEq(hist_entry.buckets, expected_buckets));

  // Verify all buckets are 0.
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(histogram.GetCount(bucket_index), 0);
  }
  END_TEST;
}

// Verifies that when flushing an histogram, all the flushed data matches that of
// the count in the histogram.
bool TestFlush() {
  BEGIN_TEST;
  FakeLogger logger;
  RemoteHistogram<kBuckets> histogram = MakeRemoteHistogram();
  fbl::Vector<HistogramBucket> expected_buckets;
  expected_buckets.reserve(histogram.size());

  // Increase the count of each bucket bucket_index times.
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    ASSERT_EQ(histogram.GetCount(bucket_index), 0);
    histogram.IncrementCount(bucket_index, bucket_index);
    ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
    expected_buckets.push_back(
        {.index = static_cast<uint32_t>(bucket_index), .count = bucket_index});
  }

  ASSERT_TRUE(histogram.Flush(&logger));

  ASSERT_FALSE(logger.logged_histograms().is_empty());

  // Check that flushed data is actually what we expect:
  // The metadata is the same, and each bucket contains bucket_index count.
  auto& hist_entry = logger.logged_histograms()[0];

  EXPECT_TRUE(hist_entry.metric_info == MakeMetricInfo());

  // Verify there is a bucket event_data.
  EXPECT_TRUE(HistEventValuesEq(hist_entry.buckets, expected_buckets));

  // Verify all buckets are 0.
  for (uint32_t bucket_index = 0; bucket_index < histogram.size(); ++bucket_index) {
    EXPECT_EQ(histogram.GetCount(bucket_index), 0);
  }
  END_TEST;
}

struct FlushArgs {
  // Pointer to the histogram which is flushing incremental snapshot to a
  // 'remote' histogram.
  RemoteHistogram<kBuckets>* histogram;

  // Pointer to the 'Remote' histogram that is accumulating the data of each
  // flush.
  BaseHistogram<kBuckets>* accumulated_histogram;

  // Used to enforce the threads start together. The main thread will signal
  // after all threads have been started.
  sync_completion_t* start;

  // Mutex used as a barrier for Flush to prevent any overlapping.
  fbl::Mutex* flush_mutex = nullptr;

  // Number of times to perform the given operation.
  size_t operations = 0;

  // Whether the thread will flush, if flase it will be incrementing the buckets.
  bool flush = false;
};

int FlushFn(void* args) {
  FlushArgs* flush_args = static_cast<FlushArgs*>(args);

  sync_completion_wait(flush_args->start, zx::sec(20).get());
  FakeLogger logger;

  for (size_t i = 0; i < flush_args->operations; ++i) {
    if (flush_args->flush) {
      fbl::AutoLock lock(flush_args->flush_mutex);
      flush_args->histogram->Flush(&logger);
    } else {
      for (uint32_t j = 0; j < kBuckets; ++j) {
        flush_args->histogram->IncrementCount(j, j);
      }
    }
  }

  for (auto& hist_entry : logger.logged_histograms()) {
    for (auto& bucket_entry : hist_entry.buckets) {
      flush_args->accumulated_histogram->IncrementCount(bucket_entry.index, bucket_entry.count);
    }
  }

  return thrd_success;
}

// Verify that under concurrent environment the final results are consistent. This
// test will have |kThreads|/2 threads increment bucket counts, and |kThreads|/2
// Flush them a certain amount of times, and collect into a BaseHistogram the final
// results. At the end, each bucket of the BaseHistogram should be the expected
// value.
bool TestFlushMultithread() {
  BEGIN_TEST;
  sync_completion_t start;
  BaseHistogram<kBuckets> accumulated;
  RemoteHistogram<kBuckets> histogram = MakeRemoteHistogram();
  fbl::Vector<thrd_t> thread_ids;
  fbl::Mutex flush_mutex;
  FlushArgs args[kThreads];

  thread_ids.reserve(kThreads);
  for (uint64_t i = 0; i < kThreads; ++i) {
    thread_ids.push_back({});
  }

  for (uint64_t i = 0; i < kThreads; ++i) {
    auto& thread_id = thread_ids[i];
    args[i].histogram = &histogram;
    args[i].accumulated_histogram = &accumulated;
    args[i].operations = i;
    args[i].flush = i % 2;
    args[i].start = &start;
    args[i].flush_mutex = &flush_mutex;
    ASSERT_EQ(thrd_create(&thread_id, FlushFn, &args[i]), thrd_success);
  }

  // Notify threads to start incrementing the count.
  sync_completion_signal(&start);

  // Wait for all threads to finish.
  for (const auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Each thread at an even position, increases the the count of a bucket by
  // bucket_index.
  constexpr size_t ceil_threads = ((kThreads - 1) / 2 * ((kThreads - 1) / 2 + 1));
  for (uint32_t i = 0; i < kBuckets; ++i) {
    // We take the sum of the accumulated and what is left, because the last
    // increment may have been scheduled after the last flush.
    EXPECT_EQ(accumulated.GetCount(i) + histogram.GetCount(i), i * ceil_threads);
  }
  END_TEST;
}

bool TestHistogramLazyInitialization() {
  BEGIN_TEST;
  // Buckets 2^i + offset.
  Histogram<kBuckets> histogram;
  histogram.Initialize(MakeHistogramOptions(), nullptr);

  histogram.Add(25);
  ASSERT_EQ(histogram.GetRemoteCount(25), 1);
  histogram.Add(25, 4);
  histogram.Add(1500, 2);

  ASSERT_EQ(histogram.GetRemoteCount(25), 5);
  ASSERT_EQ(histogram.GetRemoteCount(1500), 2);

  END_TEST;
}

bool TestAdd() {
  BEGIN_TEST;
  // Buckets 2^i + offset.
  Histogram<kBuckets> histogram(MakeHistogramOptions());

  histogram.Add(25);
  ASSERT_EQ(histogram.GetRemoteCount(25), 1);
  histogram.Add(25, 4);
  histogram.Add(1500, 2);

  ASSERT_EQ(histogram.GetRemoteCount(25), 5);
  ASSERT_EQ(histogram.GetRemoteCount(1500), 2);

  END_TEST;
}

// Verify that from the public point of view, changes are reflected accurately,
// while internally the buckets are accessed correctly.
//
// Note: The two extra buckets, are for underflow and overflow buckets.
bool TestAddMultiple() {
  BEGIN_TEST;
  HistogramOptions options = MakeHistogramOptions();
  Histogram<kBuckets> histogram(options);
  // Histogram internally allocates an overflow and underflow bucket.
  BaseHistogram<kBuckets + 2> expected_hist;

  struct ValueBucket {
    double value;
    uint32_t bucket;
  };
  fbl::Vector<ValueBucket> data;
  unsigned int seed = static_cast<unsigned int>(zx::ticks::now().get());

  // 500 random observation.
  for (int i = 0; i < 500; ++i) {
    ValueBucket curr;
    curr.bucket = rand_r(&seed) % (histogram.size());
    double min;
    double max;
    if (curr.bucket == 0) {
      min = -DBL_MAX;
    } else {
      min = options.reverse_map_fn(curr.bucket, histogram.size(), options);
    }
    max = nextafter(options.reverse_map_fn(curr.bucket + 1, histogram.size(), options), min);
    curr.value = min + (max - min) * (static_cast<double>(rand_r(&seed)) / RAND_MAX);
    ASSERT_EQ(options.map_fn(curr.value, histogram.size(), options), curr.bucket);

    Histogram<kBuckets>::Count count = 1 + rand_r(&seed) % 20;
    expected_hist.IncrementCount(curr.bucket, count);
    histogram.Add(curr.value, count);
  }

  // Verify that the data stored through public API, matches the expected values.
  for (auto& val_bucket : data) {
    EXPECT_EQ(histogram.GetRemoteCount(val_bucket.value),
              expected_hist.GetCount(val_bucket.bucket));
  }

  END_TEST;
}

// Verify we are always exposing the delta since last FlushFn.
bool TestAddAfterFlush() {
  BEGIN_TEST;
  // Buckets 2^i + offset.
  HistogramOptions options = HistogramOptions::CustomizedExponential(/*bucket_count=*/kBuckets,
                                                                     /*base=*/2,
                                                                     /*scalar=*/1, /*offset=*/-10);
  options.SetMode(MetricOptions::Mode::kEager);
  internal::FlushInterface* remote_histogram;
  FakeLogger logger;
  Histogram<kBuckets> histogram(options, &remote_histogram);
  BaseHistogram<kBuckets + 2> expected_hist;

  histogram.Add(25, 4);
  ASSERT_EQ(histogram.GetRemoteCount(25), 4);
  remote_histogram->Flush(&logger);
  histogram.Add(25, 4);
  histogram.Add(1500, 2);

  ASSERT_EQ(histogram.GetRemoteCount(25), 4);
  ASSERT_EQ(histogram.GetRemoteCount(1500), 2);

  END_TEST;
}

struct Observation {
  double value;
  Histogram<kBuckets>::Count count;
};

struct HistogramFnArgs {
  // Public histogram which is used to add observations.
  Histogram<kBuckets>* histogram;

  // When we are flushing we act as the collector, so we need
  // a pointer to the underlying histogram.
  FlushInterface* remote_histogram = nullptr;

  // We flush the contents at each step into this histogram.
  BaseHistogram<kBuckets + 2>* flushed_histogram = nullptr;

  // Synchronize thread start.
  sync_completion_t* start = nullptr;

  // Used as a barrier for preventing concurrent Flushes.
  fbl::Mutex* flush_mutex = nullptr;

  // Observations each thread will add.
  fbl::Vector<Observation>* observed_values = nullptr;

  // The thread will flush contents if set to true.
  bool flush = false;
};

// Wait until all threads are started, then start adding observations or
// flushing, depending  on the thread parameters.
int HistogramFn(void* v_args) {
  HistogramFnArgs* args = reinterpret_cast<HistogramFnArgs*>(v_args);
  FakeLogger logger;
  sync_completion_wait(args->start, zx::sec(20).get());
  for (auto& obs : *args->observed_values) {
    if (!args->flush) {
      args->histogram->Add(obs.value, obs.count);
    } else {
      fbl::AutoLock lock(args->flush_mutex);
      args->remote_histogram->Flush(&logger);
    }
  }

  for (auto& hist_entry : logger.logged_histograms()) {
    for (auto& bucket_entry : hist_entry.buckets) {
      args->flushed_histogram->IncrementCount(bucket_entry.index, bucket_entry.count);
    }
  }

  return thrd_success;
}

// Verify that when multiple threads call Add the result is eventually consistent, meaning that the
// total count in each bucket should match the count in an histogram that is being kept
// manually(BaseHistogram)
bool TestAddMultiThread() {
  BEGIN_TEST;
  // Buckets 2^i + offset.
  FlushInterface* remote_histogram;
  HistogramOptions options = MakeHistogramOptions();
  Histogram<kBuckets> histogram(options, &remote_histogram);
  BaseHistogram<kBuckets + 2> expected_hist;
  fbl::Vector<Observation> observations;

  // 1500 random observation.
  unsigned int seed = static_cast<unsigned int>(zx::ticks::now().get());
  for (int i = 0; i < 1500; ++i) {
    Observation obs;
    uint32_t bucket = rand_r(&seed) % (kBuckets + 2);
    double min;
    double max;
    if (bucket == 0) {
      min = -DBL_MAX;
    } else {
      min = options.reverse_map_fn(bucket, histogram.size(), options);
    }
    max = nextafter(options.reverse_map_fn(bucket + 1, histogram.size(), options), min);
    obs.value = min + (max - min) * (static_cast<double>(rand_r(&seed)) / RAND_MAX);
    ASSERT_EQ(options.map_fn(obs.value, histogram.size(), options), bucket);
    obs.count = 1 + rand_r(&seed) % 20;
    expected_hist.IncrementCount(bucket, kThreads * obs.count);
    observations.push_back(obs);
  }

  // Thread data.
  HistogramFnArgs args;
  sync_completion_t start;
  fbl::Vector<thrd_t> thread_ids;
  args.histogram = &histogram;
  args.start = &start;
  args.observed_values = &observations;

  for (size_t thread = 0; thread < kThreads; ++thread) {
    thread_ids.push_back({});
    auto& thread_id = thread_ids[thread];
    ASSERT_EQ(thrd_create(&thread_id, HistogramFn, &args), thrd_success);
  }
  sync_completion_signal(&start);

  for (auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Verify each bucket has the exact value as the expected histogram,
  for (uint32_t bucket = 0; bucket < kBuckets + 2; ++bucket) {
    double value;
    value = options.reverse_map_fn(bucket, histogram.size(), options);
    EXPECT_EQ(histogram.GetRemoteCount(value), expected_hist.GetCount(bucket));
  }
  END_TEST;
}

// Verify that when multiple threads call Add and Flush consistently, the result is eventually
// consistent, meaning that for each bucket, the amount in expected_hist is equal to what remains
// in the remote histogram plus the amount in the flushed hist. Essentially a sanity check that
// data is not lost.
bool TestAddAndFlushMultiThread() {
  BEGIN_TEST;
  // Buckets 2^i + offset.
  HistogramOptions options = MakeHistogramOptions();
  FlushInterface* remote_histogram;
  Histogram<kBuckets> histogram(options, &remote_histogram);
  fbl::Vector<Observation> observations;
  fbl::Mutex flush_mutex;
  BaseHistogram<kBuckets + 2> expected_hist, flushed_hist;

  // 1500 random observation.
  unsigned int seed = static_cast<unsigned int>(zx::ticks::now().get());
  for (int i = 0; i < 1500; ++i) {
    Observation obs;
    uint32_t bucket = rand_r(&seed) % (kBuckets + 2);
    double min;
    double max;
    if (bucket == 0) {
      min = -DBL_MAX;
    } else {
      min = options.reverse_map_fn(bucket, histogram.size(), options);
    }
    max = nextafter(options.reverse_map_fn(bucket + 1, histogram.size(), options), min);
    obs.value = min + (max - min) * (static_cast<double>(rand_r(&seed)) / RAND_MAX);
    ASSERT_EQ(options.map_fn(obs.value, histogram.size(), options), bucket);
    obs.count = 1 + rand_r(&seed) % 20;
    expected_hist.IncrementCount(bucket, (kThreads / 2 + kThreads % 2) * obs.count);
    observations.push_back(obs);
  }

  sync_completion_t start;
  HistogramFnArgs add_args;
  fbl::Vector<thrd_t> thread_ids;
  add_args.start = &start;
  add_args.histogram = &histogram;
  add_args.observed_values = &observations;

  HistogramFnArgs flush_args = add_args;
  flush_args.flush = true;
  flush_args.flushed_histogram = &flushed_hist;
  flush_args.remote_histogram = remote_histogram;
  flush_args.flush_mutex = &flush_mutex;

  for (size_t thread = 0; thread < kThreads; ++thread) {
    thread_ids.push_back({});
    auto& thread_id = thread_ids[thread];
    if (thread % 2 != 0) {
      ASSERT_EQ(thrd_create(&thread_id, HistogramFn, &add_args), thrd_success);
    } else {
      ASSERT_EQ(thrd_create(&thread_id, HistogramFn, &flush_args), thrd_success);
    }
  }

  sync_completion_signal(&start);

  for (auto& thread_id : thread_ids) {
    thrd_join(thread_id, nullptr);
  }

  // Verify each bucket has the exact value as the expected histogram. The addition here, is just
  // because we have no guarantee that the last flush happened after the last add. Essentially
  // what we have not yet flushed, plus what we flushed, should be equal to the expected value if
  // we didnt flush at all.
  for (uint32_t bucket = 0; bucket < histogram.size(); ++bucket) {
    double value;
    value = options.reverse_map_fn(bucket, histogram.size(), options);
    EXPECT_EQ(histogram.GetRemoteCount(value) + flushed_hist.GetCount(bucket),
              expected_hist.GetCount(bucket));
  }
  END_TEST;
}

BEGIN_TEST_CASE(BaseHistogramTest)
RUN_TEST(TestIncrement)
RUN_TEST(TestIncrementByVal)
RUN_TEST(TestIncrementMultiThread)
END_TEST_CASE(BaseHistogramTest)

BEGIN_TEST_CASE(RemoteHistogramTest)
RUN_TEST(TestFlush)
RUN_TEST(TestFlushMultithread)
RUN_TEST(TestLazyInitialization)
END_TEST_CASE(RemoteHistogramTest)

BEGIN_TEST_CASE(HistogramTest)
RUN_TEST(TestHistogramLazyInitialization)
RUN_TEST(TestAdd)
RUN_TEST(TestAddAfterFlush)
RUN_TEST(TestAddMultiple)
RUN_TEST(TestAddMultiThread)
RUN_TEST(TestAddAndFlushMultiThread)
END_TEST_CASE(HistogramTest)

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
