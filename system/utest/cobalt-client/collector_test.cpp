// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/collector.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/sync/completion.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Number of threads to spawn for multi threaded tests.
constexpr size_t kThreads = 20;
static_assert(kThreads % 2 == 0, "Use even number of threads for simplcity");

// Number of times to perform an operation in a given thread.
constexpr size_t kOperations = 50;

// Fake storage used by our FakeLogger.
template <typename T>
class FakeStorage {
public:
    T* GetOrNull(uint32_t metric_id, uint32_t event_type, uint32_t event_type_index) {
        size_t index = 0;
        if (!Find(metric_id, event_type, event_type_index, &index)) {
            return nullptr;
        }
        return entries_[index].data.get();
    };

    void InsertOrUpdateEntry(uint32_t metric_id, uint32_t event_type, uint32_t event_type_index,
                             const fbl::Function<void(fbl::unique_ptr<T>*)>& update) {
        size_t index = 0;
        if (!Find(metric_id, event_type, event_type_index, &index)) {
            entries_.push_back({.metric_id = metric_id,
                                .event_type = event_type,
                                .event_type_index = event_type_index,
                                .data = nullptr});
            index = entries_.size() - 1;
        }
        update(&entries_[index].data);
    }

private:
    bool Find(uint32_t metric_id, uint32_t event_type, uint32_t event_type_index,
              size_t* index) const {
        *index = 0;
        for (auto& entry : entries_) {
            if (entry.metric_id == metric_id && entry.event_type == event_type &&
                entry.event_type_index == event_type_index) {
                return true;
            }
            ++(*index);
        }
        return false;
    }

    // Help to identify event data logged.
    struct Entry {
        uint32_t metric_id;
        uint32_t event_type;
        uint32_t event_type_index;
        fbl::unique_ptr<T> data;
    };
    fbl::Vector<Entry> entries_;
};

// Logger for to verify that the Collector behavior is correct.
class TestLogger : public Logger {
public:
    TestLogger(FakeStorage<BaseHistogram>* histograms, FakeStorage<BaseCounter>* counters)
        : histograms_(histograms), counters_(counters), fail_(false) {}
    TestLogger(const TestLogger&) = delete;
    TestLogger(TestLogger&&) = delete;
    TestLogger& operator=(const TestLogger&) = delete;
    TestLogger& operator=(TestLogger&&) = delete;
    ~TestLogger() override = default;

    // Returns true if the histogram was persisted.
    bool Log(uint32_t metric_id, const RemoteHistogram::EventBuffer& histogram) override {
        if (!fail_.load()) {
            histograms_->InsertOrUpdateEntry(
                metric_id, histogram.metadata()[0].event_type,
                histogram.metadata()[0].event_type_index,
                [&histogram](fbl::unique_ptr<BaseHistogram>* persisted) {
                    if (*persisted == nullptr) {
                        persisted->reset(new BaseHistogram(
                            static_cast<uint32_t>(histogram.event_data().count())));
                    }
                    for (auto& bucket : histogram.event_data()) {
                        (*persisted)->IncrementCount(bucket.index, bucket.count);
                    }
                });
        }
        return !fail_.load();
    }

    // Returns true if the counter was persisted.
    bool Log(uint32_t metric_id, const RemoteCounter::EventBuffer& counter) override {
        if (!fail_.load()) {
            counters_->InsertOrUpdateEntry(metric_id, counter.metadata()[0].event_type,
                                           counter.metadata()[0].event_type_index,
                                           [&counter](fbl::unique_ptr<BaseCounter>* persisted) {
                                               if (*persisted == nullptr) {
                                                   persisted->reset(new BaseCounter());
                                               }
                                               (*persisted)->Increment(counter.event_data());
                                           });
        }
        return !fail_.load();
    }

    void set_fail(bool should_fail) { fail_.store(should_fail); }

private:
    FakeStorage<BaseHistogram>* histograms_;
    FakeStorage<BaseCounter>* counters_;
    fbl::atomic<bool> fail_;
};

CollectorOptions MakeCollectorOptions(size_t max_histograms, size_t max_counters) {
    CollectorOptions options;
    options.max_counters = max_counters;
    options.max_histograms = max_histograms;
    // Just create a dummy vmo.
    options.load_config = [](zx::vmo* vmo, size_t* size) {
        *size = 1;
        return zx::vmo::create(1, 0, vmo) == ZX_OK;
    };
    return fbl::move(options);
}

Collector MakeCollector(size_t max_histograms, size_t max_counters,
                        FakeStorage<BaseHistogram>* histograms, FakeStorage<BaseCounter>* counters,
                        TestLogger** test_logger = nullptr) {
    fbl::unique_ptr<TestLogger> logger = fbl::make_unique<TestLogger>(histograms, counters);

    if (test_logger != nullptr) {
        *test_logger = logger.get();
    }

    return fbl::move(Collector(MakeCollectorOptions(max_histograms, max_counters), fbl::move(logger)));
}

HistogramOptions MakeOptions() {
    // | .....| ....| ...| .... |
    // -inf  -2     0    2    +inf
    HistogramOptions options =
        HistogramOptions::Linear(/*bucket_count=*/2, /*scalar=*/2, /*offset=*/-2);
    return fbl::move(options);
}

// Sanity check for this codepath.
bool DebugTest() {
    BEGIN_TEST;
    Collector collector = Collector::Debug(MakeCollectorOptions(1, 1));
    auto histogram = collector.AddHistogram(0, 1, MakeOptions());
    auto counter = collector.AddCounter(0, 1);

    histogram.Add(1);
    counter.Increment();

    collector.Flush();
    END_TEST;
}

bool FishfoodTest() {
    BEGIN_TEST;
    Collector collector = Collector::Fishfood(MakeCollectorOptions(1, 1));
    auto histogram = collector.AddHistogram(0, 1, MakeOptions());
    auto counter = collector.AddCounter(0, 1);

    histogram.Add(1);
    counter.Increment();

    collector.Flush();
    END_TEST;
}

bool DogfoodTest() {
    BEGIN_TEST;
    Collector collector = Collector::Dogfood(MakeCollectorOptions(1, 1));
    auto histogram = collector.AddHistogram(0, 1, MakeOptions());
    auto counter = collector.AddCounter(0, 1);

    histogram.Add(1);
    counter.Increment();

    collector.Flush();
    END_TEST;
}

bool GeneralAvailabilityTest() {
    BEGIN_TEST;
    Collector collector = Collector::GeneralAvailability(MakeCollectorOptions(1, 1));
    auto histogram = collector.AddHistogram(0, 1, MakeOptions());
    auto counter = collector.AddCounter(0, 1);

    histogram.Add(1);
    counter.Increment();

    collector.Flush();
    END_TEST;
}

bool AddCounterTest() {
    BEGIN_TEST;
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    Collector collector =
        MakeCollector(/*max_histograms=*/0, /*max_counters=*/1, &histograms, &counters);
    auto counter = collector.AddCounter(/*metric_id=*/1, /*event_type_index=*/1);
    counter.Increment(5);
    ASSERT_EQ(counter.GetRemoteCount(), 5);
    END_TEST;
}

// Sanity check that different counters do not interfere with each other.
bool AddCounterMultipleTest() {
    BEGIN_TEST;
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    Collector collector =
        MakeCollector(/*max_histograms=*/0, /*max_counters=*/3, &histograms, &counters);
    auto counter = collector.AddCounter(/*metric_id=*/1, /*event_type_index=*/1);
    auto counter_2 = collector.AddCounter(/*metric_id=*/1, /*event_type_index=*/2);
    auto counter_3 = collector.AddCounter(/*metric_id=*/1, /*event_type_index=*/3);
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
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    Collector collector =
        MakeCollector(/*max_histograms=*/1, /*max_counters=*/0, &histograms, &counters);
    auto histogram = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 1, MakeOptions());
    histogram.Add(-4, 2);
    ASSERT_EQ(histogram.GetRemoteCount(-4), 2);
    END_TEST;
}

// Sanity check that different histograms do not interfere with each other.
bool AddHistogramMultipleTest() {
    BEGIN_TEST;
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    Collector collector =
        MakeCollector(/*max_histograms=*/3, /*max_counters=*/0, &histograms, &counters);
    auto histogram = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 1, MakeOptions());
    auto histogram_2 =
        collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 2, MakeOptions());
    auto histogram_3 =
        collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 3, MakeOptions());
    histogram.Add(-4, 2);
    histogram_2.Add(-1, 3);
    histogram_3.Add(1, 4);
    EXPECT_EQ(histogram.GetRemoteCount(-4), 2);
    EXPECT_EQ(histogram_2.GetRemoteCount(-1), 3);
    EXPECT_EQ(histogram_3.GetRemoteCount(1), 4);
    END_TEST;
}

// Verify that flushed data matches the logged data. This means that the FakeStorage has the right
// values for the right metric and event_type_index.
bool FlushTest() {
    BEGIN_TEST;
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    HistogramOptions options = MakeOptions();
    Collector collector =
        MakeCollector(/*max_histograms=*/2, /*max_counters=*/2, &histograms, &counters);
    auto histogram = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 1, options);
    auto histogram_2 = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 2, options);
    auto counter = collector.AddCounter(/*metric_id=*/2, /*event_type_index=*/1);
    auto counter_2 = collector.AddCounter(/*metric_id=*/2, /*event_type_index=*/2);

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
    EXPECT_EQ(histograms.GetOrNull(/*metric_id=*/1, /*event_type=*/0, /*event_type_index=*/1)
                  ->GetCount(options.map_fn(-4, options)),
              2);

    // -1 goes to first non underflow bucket(1)
    EXPECT_EQ(histograms.GetOrNull(1, 0, 2)->GetCount(options.map_fn(-1, options)), 3);

    EXPECT_EQ(counters.GetOrNull(2, 0, 1)->Load(), 5);
    EXPECT_EQ(counters.GetOrNull(2, 0, 2)->Load(), 3);
    END_TEST;
}

// Verify that when the logger fails to persist data, the flushed values are restored.
bool FlushFailTest() {
    BEGIN_TEST;
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    TestLogger* logger;
    HistogramOptions options = MakeOptions();
    Collector collector =
        MakeCollector(/*max_histograms=*/2, /*max_counters=*/2, &histograms, &counters, &logger);
    auto histogram = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 1, options);
    auto histogram_2 = collector.AddHistogram(/*metric_id*/ 1, /*event_type_index*/ 2, options);
    auto counter = collector.AddCounter(/*metric_id=*/2, /*event_type_index=*/1);
    auto counter_2 = collector.AddCounter(/*metric_id=*/2, /*event_type_index=*/2);

    histogram.Add(-4, 2);
    counter.Increment(5);
    collector.Flush();
    logger->set_fail(/*should_fail=*/true);

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
    EXPECT_EQ(histograms.GetOrNull(/*metric_id=*/1, /*event_type=*/0, /*event_type_index=*/1)
                  ->GetCount(options.map_fn(-4, options)),
              2);

    // -1 goes to first non underflow bucket(1), and its expected to be 0 because the logger failed.
    EXPECT_EQ(histograms.GetOrNull(1, 0, 2)->GetCount(options.map_fn(-1, options)), 0);

    EXPECT_EQ(counters.GetOrNull(2, 0, 1)->Load(), 5);

    // Expected to be 0, because the logger failed.
    EXPECT_EQ(counters.GetOrNull(2, 0, 2)->Load(), 0);
    END_TEST;
}

// All histograms have the same shape bucket for simplicity,
// and we either operate on even or odd buckets.
struct ObserveFnArgs {
    // List of histograms to operate on.
    fbl::Vector<Histogram> histograms;

    // List of counters to operate on.
    fbl::Vector<Counter> counters;

    // Number of observations to register.
    size_t count;

    // Notify the thread when to start executing.
    sync_completion_t* start;
};

int ObserveFn(void* vargs) {
    ObserveFnArgs* args = reinterpret_cast<ObserveFnArgs*>(vargs);
    static HistogramOptions options = MakeOptions();
    sync_completion_wait(args->start, zx::sec(20).get());
    size_t curr = 0;
    for (auto& hist : args->histograms) {
        for (size_t bucket_index = 0; bucket_index < options.bucket_count + 2; ++bucket_index) {
            for (size_t i = 0; i < args->count; ++i) {
                hist.Add(options.reverse_map_fn(static_cast<uint32_t>(bucket_index), options),
                         curr + bucket_index);
            }
        }
        ++curr;
    }

    curr = 0;
    for (auto& counter : args->counters) {
        for (size_t i = 0; i < args->count; ++i) {
            counter.Increment(curr);
        }
        ++curr;
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
    FakeStorage<BaseHistogram> histograms;
    FakeStorage<BaseCounter> counters;
    HistogramOptions options = MakeOptions();
    sync_completion_t start;

    ObserveFnArgs observe_args;
    observe_args.start = &start;
    observe_args.count = kOperations;
    TestLogger* logger;

    Collector collector =
        MakeCollector(/*max_histograms=*/9, /*max_counters=*/9, &histograms, &counters, &logger);

    for (uint32_t metric_id = 0; metric_id < 3; ++metric_id) {
        for (uint32_t event_type_index = 1; event_type_index < 4; ++event_type_index) {
            observe_args.histograms.push_back(
                collector.AddHistogram(2 * metric_id, event_type_index, options));
            observe_args.counters.push_back(
                collector.AddCounter(2 * metric_id + 1, event_type_index));
        }
    }
    // Add empty entries to the fake storage.
    collector.Flush();
    // Set the logger to either fail to persist or succeed.
    logger->set_fail(should_fail);

    FlushFnArgs flush_args;
    flush_args.collector = &collector;
    flush_args.count = kOperations;
    flush_args.start = &start;

    fbl::Vector<thrd_t> thread_ids;

    thread_ids.reserve(kThreads);
    for (size_t i = 0; i < kThreads; ++i) {
        thrd_t thread_id;
        if (i % 2 == 0) {
            thrd_create(&thread_id, &ObserveFn, &observe_args);
        } else {
            thrd_create(&thread_id, &FlushFn, &flush_args);
        }
        thread_ids.push_back(thread_id);
    }

    // Start all threads.
    sync_completion_signal(&start);

    for (auto thread_id : thread_ids) {
        ASSERT_EQ(thrd_join(thread_id, nullptr), thrd_success);
    }

    // Verify that all histograms buckets and counters have exactly |kOperations| * |kThreads| /
    // 2 count.
    constexpr size_t target_count = kThreads * kOperations / 2;
    for (uint32_t metric_id = 0; metric_id < 3; ++metric_id) {
        for (uint32_t event_type_index = 1; event_type_index < 4; ++event_type_index) {
            size_t index = 3 * metric_id + event_type_index - 1;
            auto* tmp_hist = histograms.GetOrNull(2 * metric_id, 0, event_type_index);
            // Each bucket is increased |index| + |i| for each thread recording observations.
            for (uint32_t i = 0; i < 4; ++i) {
                ASSERT_TRUE(tmp_hist != nullptr);
                EXPECT_EQ(tmp_hist->GetCount(i) + observe_args.histograms[index].GetRemoteCount(
                                                      options.reverse_map_fn(i, options)),
                          target_count * (i + index));
            }

            auto* tmp_counter = counters.GetOrNull(2 * metric_id + 1, 0, event_type_index);
            ASSERT_TRUE(tmp_counter != nullptr);
            // Each counter is increased by |index| for each thread recording observations.
            EXPECT_EQ(tmp_counter->Load() + observe_args.counters[index].GetRemoteCount(),
                      target_count * index);
        }
    }
    END_TEST;
}

BEGIN_TEST_CASE(CollectorTest)
RUN_TEST(AddCounterTest)
RUN_TEST(AddCounterMultipleTest)
RUN_TEST(AddHistogramTest)
RUN_TEST(AddHistogramMultipleTest)
RUN_TEST(FlushTest)
RUN_TEST(FlushFailTest)
RUN_TEST(FlushFailTest)
RUN_TEST(FlushMultithreadTest<false>)
RUN_TEST(FlushMultithreadTest<true>)
RUN_TEST(GeneralAvailabilityTest)
RUN_TEST(DogfoodTest)
RUN_TEST(FishfoodTest)
RUN_TEST(DebugTest)
END_TEST_CASE(CollectorTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
