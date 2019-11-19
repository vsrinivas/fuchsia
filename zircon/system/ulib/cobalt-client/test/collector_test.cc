// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

#include "cobalt-client/cpp/types-internal.h"
#include "fake_logger.h"

namespace cobalt_client {
namespace internal {
namespace {

// Number of threads to spawn for multi threaded tests.
constexpr size_t kThreads = 10;
static_assert(kThreads % 2 == 0, "Use even number of threads for simplcity");

// Number of times to perform an operation in a given thread.
constexpr size_t kOperations = 50;

// Project Name.
constexpr char kProjectName[] = "MyName";

// Metric Id to be used by default MetricOptions.
constexpr uint32_t kMetricId = 1;

// Number of buckets used for histogram.
constexpr uint32_t kBuckets = 5;

// Event code to be used by default MetricOptions.
constexpr std::array<uint32_t, MetricInfo::kMaxEventCodes> kEventCodes = {1, 2, 3, 4, 5};

// Component name used for the tests.
constexpr char kComponent[] = "SomeRandomCollectorComponent";

MetricInfo MakeMetricInfo(uint32_t metric_id = kMetricId, uint32_t event_code_0 = 0,
                          uint32_t event_code_1 = 0, uint32_t event_code_2 = 0,
                          uint32_t event_code_3 = 0, uint32_t event_code_4 = 0) {
  MetricInfo metric_info;
  metric_info.component = kComponent;
  metric_info.metric_id = metric_id;
  metric_info.event_codes = {event_code_0, event_code_1, event_code_2, event_code_3, event_code_4};
  return metric_info;
}

CollectorOptions MakeCollectorOptions() {
  CollectorOptions options = CollectorOptions::Debug();
  // Just create a dummy vmo.
  return options;
}

MetricOptions MakeMetricOptionsWithDefault() {
  MetricOptions options;
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = kMetricId;
  options.event_codes = kEventCodes;
  options.component = kComponent;
  return options;
}

MetricOptions MakeMetricOptions(uint32_t metric_id = kMetricId, uint32_t event_code_0 = 0,
                                uint32_t event_code_1 = 0, uint32_t event_code_2 = 0,
                                uint32_t event_code_3 = 0, uint32_t event_code_4 = 0) {
  MetricOptions options;
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = metric_id;
  options.event_codes = {event_code_0, event_code_1, event_code_2, event_code_3, event_code_4};
  options.component = kComponent;
  return options;
}

HistogramOptions MakeHistogramOptionsWithDefault() {
  // | .....| ....| ...| .... |
  // -inf  -2     0    2    +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(kBuckets, /*scalar*/ 2, /*offset*/ -2);
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = kMetricId;
  options.event_codes = kEventCodes;
  options.component = kComponent;
  return options;
}

HistogramOptions MakeHistogramOptions(uint32_t metric_id = kMetricId, uint32_t event_code_0 = 0,
                                      uint32_t event_code_1 = 0, uint32_t event_code_2 = 0,
                                      uint32_t event_code_3 = 0, uint32_t event_code_4 = 0) {
  // | .....| ....| ...| .... |
  // -inf  -2     0    2    +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(kBuckets, /*scalar*/ 2, /*offset*/ -2);
  options.SetMode(MetricOptions::Mode::kEager);
  options.metric_id = metric_id;
  options.event_codes = {event_code_0, event_code_1, event_code_2, event_code_3, event_code_4};
  options.component = kComponent;
  return options;
}

// Sanity check for this codepath.
bool TestDebug() {
  BEGIN_TEST;
  CollectorOptions options = CollectorOptions::Debug();
  ASSERT_EQ(options.release_stage, static_cast<uint32_t>(ReleaseStage::kDebug));
  END_TEST;
}

bool TestFishfood() {
  BEGIN_TEST;
  CollectorOptions options = CollectorOptions::Fishfood();
  ASSERT_EQ(options.release_stage, static_cast<uint32_t>(ReleaseStage::kFishfood));
  END_TEST;
}

bool TestDogfood() {
  BEGIN_TEST;
  CollectorOptions options = CollectorOptions::Dogfood();
  ASSERT_EQ(options.release_stage, static_cast<uint32_t>(ReleaseStage::kDogfood));
  END_TEST;
}

bool TestGeneralAvailability() {
  BEGIN_TEST;
  CollectorOptions options = CollectorOptions::GeneralAvailability();
  ASSERT_EQ(options.release_stage, static_cast<uint32_t>(ReleaseStage::kGa));
  END_TEST;
}

// Sanity Check
bool ConstructFromOptionsTest() {
  BEGIN_TEST;
  CollectorOptions options = MakeCollectorOptions();
  options.project_name = kProjectName;
  Collector collector = Collector(std::move(options));
  // Sanity check nothing crashes.
  auto histogram = Histogram<kBuckets>(MakeHistogramOptionsWithDefault(), &collector);
  auto counter = Counter(MakeMetricOptionsWithDefault(), &collector);

  histogram.Add(1);
  counter.Increment();

  collector.Flush();
  END_TEST;
}

bool AddCounterTest() {
  BEGIN_TEST;
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  Collector collector(std::move(logger));
  auto counter = Counter(MakeMetricOptionsWithDefault(), &collector);
  counter.Increment(5);
  ASSERT_EQ(counter.GetRemoteCount(), 5);
  END_TEST;
}

// Sanity check that different counters do not interfere with each other.
bool AddCounterMultipleTest() {
  BEGIN_TEST;
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  Collector collector(std::move(logger));

  auto counter = Counter(MakeMetricOptions(1, 1), &collector);
  auto counter_2 = Counter(MakeMetricOptions(1, 2), &collector);
  auto counter_3 = Counter(MakeMetricOptions(1, 3), &collector);
  counter.Increment(5);
  counter_2.Increment(3);
  counter_3.Increment(2);
  ASSERT_EQ(counter.GetRemoteCount(), 5);
  ASSERT_EQ(counter_2.GetRemoteCount(), 3);
  ASSERT_EQ(counter_3.GetRemoteCount(), 2);
  END_TEST;
}

bool AddHistogramTest() {
  BEGIN_TEST;
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  Collector collector(std::move(logger));

  auto histogram = Histogram<kBuckets>(MakeHistogramOptionsWithDefault(), &collector);

  histogram.Add(-4, 2);
  ASSERT_EQ(histogram.GetRemoteCount(-4), 2);
  END_TEST;
}

// Sanity check that different histograms do not interfere with each other.
bool AddHistogramMultipleTest() {
  BEGIN_TEST;
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  Collector collector(std::move(logger));

  auto histogram =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 1), &collector);
  auto histogram_2 =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 2), &collector);
  auto histogram_3 =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 3), &collector);

  histogram.Add(-4, 2);
  histogram_2.Add(-1, 3);
  histogram_3.Add(1, 4);
  EXPECT_EQ(histogram.GetRemoteCount(-4), 2);
  EXPECT_EQ(histogram_2.GetRemoteCount(-1), 3);
  EXPECT_EQ(histogram_3.GetRemoteCount(1), 4);
  END_TEST;
}

// Verify that flushed data matches the logged data. This means that the FakeStorage has the right
// values for the right metric and event_code.
bool FlushTest() {
  BEGIN_TEST;
  HistogramOptions options = MakeHistogramOptionsWithDefault();
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  FakeLogger* logger_ptr = logger.get();
  Collector collector(std::move(logger));

  auto histogram =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 1), &collector);
  auto histogram_2 =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 2), &collector);
  auto counter = Counter(MakeMetricOptions(2, 1), &collector);
  auto counter_2 = Counter(MakeMetricOptions(2, 2), &collector);

  histogram.Add(-4, 2);
  histogram_2.Add(-1, 3);
  counter.Increment(5);
  counter_2.Increment(3);

  collector.Flush();

  // Verify reset of local data.
  EXPECT_EQ(histogram.GetRemoteCount(-4), 0);
  EXPECT_EQ(histogram_2.GetRemoteCount(-1), 0);
  EXPECT_EQ(counter.GetRemoteCount(), 0);
  EXPECT_EQ(counter_2.GetRemoteCount(), 0);

  // Verify 'persisted' data matches what the local data used to be.
  // Note: for now event_type is 0 for all metrics.

  // -4 goes to underflow bucket(0)
  size_t bucket = options.map_fn(-4, histogram.size(), options);
  EXPECT_EQ(logger_ptr->GetHistogram(MakeMetricInfo(1, 1))[bucket].count, 2);

  // -1 goes to first non underflow bucket(1)
  bucket = options.map_fn(-1, histogram_2.size(), options);
  EXPECT_EQ(logger_ptr->GetHistogram(MakeMetricInfo(1, 2))[bucket].count, 3);

  EXPECT_EQ(logger_ptr->GetCounter(MakeMetricInfo(2, 1)), 5);
  EXPECT_EQ(logger_ptr->GetCounter(MakeMetricInfo(2, 2)), 3);
  END_TEST;
}

// Verify that when the logger fails to persist data, the flushed values are restored.
bool FlushFailTest() {
  BEGIN_TEST;
  HistogramOptions options = MakeHistogramOptions();
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  FakeLogger* logger_ptr = logger.get();
  Collector collector(std::move(logger));

  auto histogram =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 1), &collector);
  auto histogram_2 =
      Histogram<kBuckets>(MakeHistogramOptions(/*metric_id*/ 1, /*event_code*/ 2), &collector);
  auto counter = Counter(MakeMetricOptions(2, 1), &collector);
  auto counter_2 = Counter(MakeMetricOptions(2, 2), &collector);

  histogram.Add(-4, 2);
  counter.Increment(5);
  collector.Flush();
  logger_ptr->set_should_fail(/*should_fail*/ true);

  histogram_2.Add(-1, 3);
  counter_2.Increment(3);

  collector.Flush();

  // Verify reset of local data.
  EXPECT_EQ(histogram.GetRemoteCount(-4), 0);
  EXPECT_EQ(histogram_2.GetRemoteCount(-1), 3);
  EXPECT_EQ(counter.GetRemoteCount(), 0);
  EXPECT_EQ(counter_2.GetRemoteCount(), 3);

  // Verify 'persisted' data matches what the local data used to be.
  // Note: for now event_type is 0 for all metrics.

  // -4 goes to underflow bucket(0)
  size_t bucket = options.map_fn(-4, histogram.size(), options);
  EXPECT_EQ(logger_ptr->GetHistogram(MakeMetricInfo(1, 1))[bucket].count, 2);

  // -1 goes to first non underflow bucket(1), and its expected to be 0 because the logger failed.
  bucket = options.map_fn(-1, histogram.size(), options);
  EXPECT_EQ(logger_ptr->GetHistogram(MakeMetricInfo(1, 2))[bucket].count, 0);

  EXPECT_EQ(logger_ptr->GetCounter(MakeMetricInfo(2, 1)), 5);

  // Expected to be 0, because the logger failed.
  EXPECT_EQ(logger_ptr->GetCounter(MakeMetricInfo(2, 2)), 0);
  END_TEST;
}

// All histograms have the same shape bucket for simplicity,
// and we either operate on even or odd buckets.
struct ObserveFnArgs {
  // List of histograms to operate on.
  fbl::Vector<std::unique_ptr<Histogram<kBuckets>>>* histograms;

  // List of counters to operate on.
  fbl::Vector<std::unique_ptr<Counter>>* counters;

  // Number of observations to register.
  size_t count;

  // Amount increased by each observation or the weight of each observation.
  size_t amount;

  // Notify the thread when to start executing.
  sync_completion_t* start;
};

static const HistogramOptions kDefaultObserverOptions = MakeHistogramOptionsWithDefault();

int ObserveFn(void* vargs) {
  ObserveFnArgs* args = reinterpret_cast<ObserveFnArgs*>(vargs);
  const auto& options = kDefaultObserverOptions;
  sync_completion_wait(args->start, zx::sec(20).get());
  for (auto& hist : *(args->histograms)) {
    for (size_t bucket_index = 0; bucket_index < hist->size(); ++bucket_index) {
      double value =
          options.reverse_map_fn(static_cast<uint32_t>(bucket_index), hist->size(), options);
      // Each bucket is incremented by |amount|
      for (size_t i = 0; i < args->count; ++i) {
        hist->Add(value, args->amount);
      }
    }
  }

  for (auto& counter : *args->counters) {
    for (size_t i = 0; i < args->count; ++i) {
      counter->Increment(args->amount);
    }
  }
  return thrd_success;
}

struct FlushFnArgs {
  // Target collector to be flushed.
  Collector* collector;

  // Number of times to flush.
  size_t count;

  // Notify thread start.
  sync_completion_t* start;
};

int FlushFn(void* vargs) {
  FlushFnArgs* args = reinterpret_cast<FlushFnArgs*>(vargs);

  sync_completion_wait(args->start, zx::sec(20).get());
  for (size_t i = 0; i < args->count; ++i) {
    args->collector->Flush();
  }
  return thrd_success;
}

// Verify that if we flush while the histograms and counters are being updated,
// no data is lost, meaning that the sum of the persisted data and the local data
// is equal to the expected value.
template <bool should_fail>
bool FlushMultithreadTest() {
  BEGIN_TEST;
  HistogramOptions options = MakeHistogramOptionsWithDefault();
  // Preallocate with the number of logs to prevent realloc problems.
  std::unique_ptr<FakeLogger> logger = std::make_unique<FakeLogger>();
  FakeLogger* logger_ptr = logger.get();
  Collector collector(std::move(logger));
  logger_ptr->set_should_fail(should_fail);
  fbl::Vector<std::unique_ptr<Histogram<kBuckets>>> histograms;
  fbl::Vector<std::unique_ptr<Counter>> counters;
  fbl::Vector<ObserveFnArgs> observe_args;
  fbl::Vector<thrd_t> thread_ids;
  FlushFnArgs flush_args;
  sync_completion_t start;

  // Create an histogram and a counter for each combination of metric and event codes.
  // The metric_id and the event_code can be translated into the index in the respective vector as
  // follows: index = 3 * metric_id + event_code - 1.
  for (uint32_t metric_id = 0; metric_id < 4; ++metric_id) {
    for (uint32_t event_code = 0; event_code < 3; ++event_code) {
      histograms.push_back(std::make_unique<Histogram<kBuckets>>(
          MakeHistogramOptions(metric_id, event_code), &collector));
      counters.push_back(std::make_unique<Counter>(MakeMetricOptions(metric_id, event_code)));
    }
  }
  // Instantiate threads to operate in the given structures.
  // Odd Threads -> produce |amount| observations for each bucket and counter.
  // Even Threads -> flush observations.
  flush_args.collector = &collector;
  flush_args.count = kOperations;
  flush_args.start = &start;

  size_t expected_bucket_count = 0;
  observe_args.reserve(kThreads);
  thread_ids.reserve(kThreads);
  for (size_t thread_index = 0; thread_index < kThreads; ++thread_index) {
    thread_ids.push_back({});
    if (thread_index % 2 == 0) {
      thrd_create(&thread_ids[thread_index], &FlushFn, &flush_args);
    } else {
      ObserveFnArgs args;
      args.amount = kOperations;
      args.count = thread_index;
      args.histograms = &histograms;
      args.counters = &counters;
      args.start = &start;
      observe_args.push_back(args);
      expected_bucket_count += thread_index * kOperations;
      thrd_create(&thread_ids[thread_index], &ObserveFn, &observe_args[observe_args.size() - 1]);
    }
  }

  // Start all threads.
  sync_completion_signal(&start);

  for (auto thread_id : thread_ids) {
    ASSERT_EQ(thrd_join(thread_id, nullptr), thrd_success);
  }

  collector.Flush();

  for (uint32_t metric_id = 0; metric_id < 4; ++metric_id) {
    for (uint32_t event_code = 0; event_code < 3; ++event_code) {
      for (auto& hist : histograms) {
        for (size_t bucket = 0; bucket < hist->size(); ++bucket) {
          double value =
              options.reverse_map_fn(static_cast<uint32_t>(bucket), hist->size(), options);
          auto& logged_hist = logger_ptr->GetHistogram(MakeMetricInfo(metric_id, event_code));
          EXPECT_EQ((should_fail ? hist->GetRemoteCount(value) : logged_hist[bucket].count),
                    expected_bucket_count);
        }
      }
      for (auto& counter : counters) {
        int64_t logged_count = logger_ptr->GetCounter(MakeMetricInfo(metric_id, event_code));
        ASSERT_EQ(logged_count + counter->GetRemoteCount(), expected_bucket_count);
      }
    }
  }

  END_TEST;
}

BEGIN_TEST_CASE(CollectorOptionsTest)
RUN_TEST(TestDebug)
RUN_TEST(TestFishfood)
RUN_TEST(TestDogfood)
RUN_TEST(TestGeneralAvailability)
END_TEST_CASE(CollectorOptionsTest)

BEGIN_TEST_CASE(CollectorTest)
RUN_TEST(ConstructFromOptionsTest)
RUN_TEST(AddCounterTest)
RUN_TEST(AddCounterMultipleTest)
RUN_TEST(AddHistogramTest)
RUN_TEST(AddHistogramMultipleTest)
RUN_TEST(FlushTest)
RUN_TEST(FlushFailTest)
RUN_TEST(FlushMultithreadTest<false>)
RUN_TEST(FlushMultithreadTest<true>)
END_TEST_CASE(CollectorTest)

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
