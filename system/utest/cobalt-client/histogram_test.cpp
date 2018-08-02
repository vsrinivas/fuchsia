// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/observation.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace {
using cobalt_client::Counter;
using cobalt_client::internal::BaseHistogram;

// Number of threads to use for multithreading sanity check.
constexpr uint32_t kThreads = 20;

// Scalar used in UpdateHistogramFn to calculate the number of operations
// per bucket.
constexpr uint64_t kUpdateScalar = 100;

constexpr uint64_t kMetricId = 5;

constexpr uint32_t kEncodingId = 1;

constexpr uint32_t kBuckets = 20;

constexpr char kName[] = "SomeName";

bool Contains(const fbl::Vector<uint64_t>& container, uint64_t val) {
    for (auto el : container) {
        if (el == val) {
            return true;
        }
    }
    return false;
}

ObservationValue MakeObservationValue(const char* name, Value value) {
    ObservationValue obs;
    obs.name.size = strlen(name) + 1;
    obs.name.data = const_cast<char*>(name);
    obs.value = value;
    obs.encoding_id = kEncodingId;
    return obs;
}

const fbl::Vector<ObservationValue>& GetMetadata() {
    static const char part_name[] = "part";

    static const fbl::Vector<ObservationValue> metadata = {
        MakeObservationValue(part_name, IntValue(24)),
        MakeObservationValue(part_name, DoubleValue(0.125)),
    };

    return metadata;
}

BaseHistogram MakeHistogram() {
    BaseHistogram histogram(kName, GetMetadata(), kBuckets, kMetricId, kEncodingId);
    return histogram;
}

struct CheckContentsFlushFn {
    void operator()(uint64_t metric_id, const fidl::VectorView<ObservationValue>& observations,
                    BaseHistogram::FlushCompleteFn complete_fn) const {
        const fbl::Vector<ObservationValue>& metadata = GetMetadata();
        auto complete_flush = fbl::MakeAutoCall(fbl::move(complete_fn));

        size_t expected_size = metadata.size() + 1;
        if (observations.count() < expected_size) {
            *error = fbl::StringPrintf("observations.count()(%lu) != expected_size(%lu)\n",
                                       observations.count(), expected_size);
            return;
        }

        fbl::Vector<size_t> visited_obs;
        for (size_t curr_meta = 0; curr_meta < metadata.size(); ++curr_meta) {
            const auto& meta_obs = metadata[curr_meta];
            bool found_match = false;
            for (size_t current_obs = 0; current_obs < metadata.size(); ++current_obs) {
                if (Contains(visited_obs, current_obs)) {
                    continue;
                }
                const ObservationValue& value = observations[current_obs];
                if (value.encoding_id != encoding_id) {
                    *error = fbl::StringPrintf(
                        "observations[%lu].encoding_id(%u) != expected_encoding_id(%u)\n",
                        current_obs, meta_obs.encoding_id, encoding_id);
                    return;
                }
                if (memcmp(meta_obs.name.data, value.name.data, value.name.size) == 0 &&
                    memcmp(&meta_obs.value, &value.value, sizeof(Value)) == 0) {
                    found_match = true;
                    visited_obs.push_back(current_obs);
                    break;
                }
            }
            if (!found_match) {
                *error = fbl::StringPrintf("metadata[%lu] is not in observations.\n", curr_meta);
                return;
            }
        }

        size_t histogram_index = metadata.size();
        const ObservationValue& hist_obs = observations[histogram_index];
        if (hist_obs.encoding_id != encoding_id) {
            *error =
                fbl::StringPrintf("observations[%lu].encoding_id(%u) != expected_encoding_id(%u)\n",
                                  histogram_index, hist_obs.encoding_id, encoding_id);
            return;
        }

        if (memcmp(histogram_name.c_str(), hist_obs.name.data, hist_obs.name.size) != 0) {
            *error = fbl::StringPrintf("observations[%lu].name(%*s) != histogram_name(%s)\n",
                                       histogram_index, static_cast<int>(hist_obs.name.size),
                                       hist_obs.name.data, histogram_name.c_str());
            return;
        }

        // Check bucket values.
        if (hist_obs.value.tag != fuchsia_cobalt_ValueTagint_bucket_distribution) {
            *error = fbl::StringPrintf(
                "observations[%lu].value not IntBucketDistribution. tag(%u) != %u\n",
                histogram_index, hist_obs.value.tag,
                fuchsia_cobalt_ValueTagint_bucket_distribution);
            return;
        }

        if (hist_obs.value.int_bucket_distribution.count != bucket_values.size()) {
            *error =
                fbl::StringPrintf("observations[%lu].value.int_bucket_distribution.count(%lu) "
                                  "!= bucket_values.size()(%lu)",
                                  histogram_index, hist_obs.value.int_bucket_distribution.count,
                                  bucket_values.size());
            return;
        }

        DistributionEntry* buckets =
            static_cast<DistributionEntry*>(hist_obs.value.int_bucket_distribution.data);
        for (size_t bucket_index = 0; bucket_index < bucket_values.size(); ++bucket_index) {
            bool index_found = false;
            for (size_t bucket = 0; bucket < bucket_values.size(); ++bucket) {
                if (buckets[bucket].index == bucket_index) {
                    index_found = true;
                    if (buckets[bucket].count != bucket_values[bucket_index]) {
                        *error = fbl::StringPrintf(
                            "bucket_value[%lu](%lu) != buckets[%lu].count(%lu), but index match!\n",
                            bucket_index, bucket_values[bucket], bucket, buckets[bucket].count);
                        return;
                    }
                    break;
                }
            }
            if (!index_found) {
                *error = fbl::StringPrintf(
                    "bucket at index %lu is missing from the observed buckets.\n", bucket_index);
                return;
            }
        }
    }

    fbl::Vector<uint64_t> bucket_values;
    fbl::String histogram_name;

    fbl::String* error;
    uint32_t encoding_id;
};

// Add an observation and verify the bucket is updated.
bool AddObservationTest() {
    BEGIN_TEST;
    BaseHistogram histogram = MakeHistogram();
    ASSERT_EQ(histogram.GetCount(/*bucket=*/10), 0);
    histogram.IncrementCount(/*bucket=*/10);
    ASSERT_EQ(histogram.GetCount(10), 1);
    END_TEST;
}

// Verifies the order and the data in the elements when flushed, everything in the histogram
// should be flushed. This is true only for single threaded environment, since operation reordering,
// might send stale version of the bucket, but everything will eventually be flushed, so eventually
// all data will handled in multi-threaded environment.
bool FlushTest() {
    BEGIN_TEST;
    fbl::String error;
    CheckContentsFlushFn handler;
    handler.histogram_name = kName;
    handler.encoding_id = kEncodingId;
    handler.error = &error;
    BaseHistogram histogram = MakeHistogram();
    // bucket[i] = i
    for (uint64_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
        for (uint64_t op = 0; op < bucket_index; ++op) {
            histogram.IncrementCount(/*bucket=*/bucket_index);
        }
        handler.bucket_values.push_back(bucket_index);
    }
    ASSERT_TRUE(histogram.Flush(fbl::move(handler)));
    ASSERT_TRUE(error.empty(), error.c_str());
    END_TEST;
}

bool FlushWhileFlushingTest() {
    BEGIN_TEST;
    BaseHistogram::FlushCompleteFn complete_cb;
    BaseHistogram histogram = MakeHistogram();

    ASSERT_TRUE(histogram.Flush([&complete_cb](uint64_t metric_id,
                                               const fidl::VectorView<ObservationValue>& obs,
                                               BaseHistogram::FlushCompleteFn complete_fn) {
        complete_cb = fbl::move(complete_fn);
    }));
    ASSERT_FALSE(histogram.Flush(BaseHistogram::FlushFn()));
    complete_cb();
    ASSERT_TRUE(histogram.Flush([](uint64_t, const fidl::VectorView<ObservationValue>&,
                                   BaseHistogram::FlushCompleteFn) {}));
    ASSERT_FALSE(histogram.Flush(BaseHistogram::FlushFn()));
    END_TEST;
}

struct UpdateHistogramFnArgs {
    BaseHistogram* histogram;
    sync_completion_t* completion;
};

int UpdateHistogramFn(void* void_args) {
    UpdateHistogramFnArgs* args = static_cast<UpdateHistogramFnArgs*>(void_args);

    // Wait for call.
    zx_status_t res = sync_completion_wait(args->completion, zx::sec(20).get());
    if (res != ZX_OK) {
        return thrd_error;
    }

    for (uint64_t bucket = 0; bucket < kBuckets; ++bucket) {
        for (uint64_t i = 0; i < kUpdateScalar * bucket; ++i) {
            args->histogram->IncrementCount(bucket);
        }
    }

    return thrd_success;
}

// Verify that incrementing each bucket |kUpdateScalar|*|bucket_index| times per thread in
// |kThreads| threads, have a consistent view of the data, which allows the histogram to be
// thread-safe through this operations.
bool MultiThreadCountOpsConsistencyTest() {
    BEGIN_TEST;
    BaseHistogram histogram = MakeHistogram();
    fbl::Vector<thrd_t> thread_ids;
    sync_completion_t wait_for_start;

    UpdateHistogramFnArgs args;
    args.histogram = &histogram;
    args.completion = &wait_for_start;

    // Initialize all threads then signal start, and join all fo them.
    thread_ids.reserve(kThreads);
    for (uint32_t i = 0; i < kThreads; ++i) {
        thread_ids.push_back(0);
    }

    for (auto& thread_id : thread_ids) {
        ASSERT_EQ(thrd_create(&thread_id, UpdateHistogramFn, &args), thrd_success);
    }
    sync_completion_signal(&wait_for_start);

    for (auto& thread_id : thread_ids) {
        int res = thrd_success;
        ASSERT_EQ(thrd_join(thread_id, &res), thrd_success);
        ASSERT_EQ(res, thrd_success);
    }

    // Verify the result is coherent.
    // bucket[i] = i * kUpdateScalar * kThreads
    for (uint64_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
        ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index * kUpdateScalar * kThreads);
    }
    END_TEST;
}

struct WaitBeforeCompleteFlushHandler {
    void operator()(uint64_t metric_id, const fidl::VectorView<ObservationValue>& observations,
                    BaseHistogram::FlushCompleteFn complete_fn) {
        ZX_ASSERT(thread_ids != nullptr);
        ZX_ASSERT(flushing_thread != nullptr);

        for (auto thread_id : *thread_ids) {
            if (thread_id != thrd_current()) {
                int res;
                thrd_join(thread_id, &res);
            }
        }

        *flushing_thread = thrd_current();
        counter->Increment();
        complete_fn();
        sync_completion_signal(completion);
    }

    // List of all threads that were spawned to flush.
    fbl::Vector<thrd_t>* thread_ids;

    // Notify main thread that all threads finished while this thread was flushing.
    sync_completion_t* completion;

    // Counter for the number of times Flush was actually called.
    Counter* counter;

    // Main thread will join this thread.
    thrd_t* flushing_thread;
};

struct FlushHistogramFnArgs {
    BaseHistogram* histogram;
    // FlushHandler
    WaitBeforeCompleteFlushHandler flush_handler;

    // Notify to start the threads together.
    sync_completion_t* start;
};

// This verifies that while a thread is flushing, all other threads fail to flush the contents
// of the histogram thus any action that needs to be taken over the observation buffer is safe,
// as long as.
int FlushHistogramFn(void* void_args) {
    FlushHistogramFnArgs* args = static_cast<FlushHistogramFnArgs*>(void_args);
    sync_completion_wait(args->start, zx::sec(20).get());
    args->histogram->Flush(fbl::move(args->flush_handler));
    return thrd_success;
}

bool MultiThreadFlushOpsConsistencyTest() {
    BEGIN_TEST;
    BaseHistogram histogram = MakeHistogram();
    FlushHistogramFnArgs args[kThreads];
    fbl::Vector<thrd_t> thread_ids;
    sync_completion_t wait_for_start, wait_for_completion;
    thrd_t flushing_thread;
    Counter flushes(0, 0);

    // Initialize all threads then signal start, and join all fo them.
    thread_ids.reserve(kThreads);
    for (uint32_t i = 0; i < kThreads; ++i) {
        thread_ids.push_back(0);
    }

    for (uint64_t thread = 0; thread < kThreads; ++thread) {
        auto& thread_id = thread_ids[thread];
        auto handler = WaitBeforeCompleteFlushHandler();
        handler.thread_ids = &thread_ids;
        handler.completion = &wait_for_completion;
        handler.counter = &flushes;
        handler.flushing_thread = &flushing_thread;
        args[thread].flush_handler = fbl::move(handler);
        args[thread].histogram = &histogram;
        args[thread].start = &wait_for_start;
        ASSERT_EQ(thrd_create(&thread_id, FlushHistogramFn, &args), thrd_success);
    }
    sync_completion_signal(&wait_for_start);
    sync_completion_wait(&wait_for_completion, zx::sec(20).get());

    // Wait for the flushing thread to finish.
    thrd_join(flushing_thread, nullptr);

    // Verify the number of times it was flushed, must be one.
    ASSERT_EQ(flushes.Load(), 1);

    // Verify that if we flush again it succeeds.
    ASSERT_TRUE(histogram.Flush([](uint64_t, const fidl::VectorView<ObservationValue>&,
                                   BaseHistogram::FlushCompleteFn) {}));

    ASSERT_FALSE(histogram.Flush([](uint64_t, const fidl::VectorView<ObservationValue>&,
                                    BaseHistogram::FlushCompleteFn) {}));
    END_TEST;
}

BEGIN_TEST_CASE(BaseHistogramTest)
RUN_TEST(AddObservationTest)
RUN_TEST(FlushTest)
RUN_TEST(FlushWhileFlushingTest)
RUN_TEST(MultiThreadCountOpsConsistencyTest)
RUN_TEST(MultiThreadFlushOpsConsistencyTest)
END_TEST_CASE(BaseHistogramTest)
} // namespace
} // namespace cobalt_client
