// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <cobalt-client/cpp/histogram-internal.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Number of threads for running multi threaded tests.
constexpr uint64_t kThreads = 20;

// Number of buckets used for histogram(CUT).
constexpr uint32_t kBuckets = 40;

// Default id for the histogram.
constexpr uint64_t kMetricId = 1;

// Returns an immutable vector of metadata.
fbl::Vector<Metadata>& GetMetadata() {
    static fbl::Vector<Metadata> metadata = {{/*event_type =*/1, /*event_type_index =*/2},
                                             {/*event_type =*/2, /*event_type_index =*/4}};
    return metadata;
}

// Returns true if both vectors contains the same metadata entries in the same order.
bool MetadataEq(const fbl::Vector<Metadata>& lhs, const fbl::Vector<Metadata>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t i = 0; i < rhs.size(); ++i) {
        if (memcmp(&lhs[i], &rhs[i], sizeof(Metadata)) != 0) {
            return false;
        }
    }

    return true;
}

RemoteHistogram MakeRemoteHistogram() {
    return RemoteHistogram(kBuckets, kMetricId, GetMetadata());
}

bool HistEventValuesEq(fidl::VectorView<HistogramBucket> actual,
                       fidl::VectorView<HistogramBucket> expected) {
    BEGIN_HELPER;
    ASSERT_EQ(actual.count(), expected.count());
    for (size_t i = 0; i < actual.count(); ++i) {
        HistogramBucket& actual_bucket = actual[i];
        bool found = false;
        for (size_t j = 0; j < expected.count(); ++j) {
            HistogramBucket& expected_bucket = expected[j];
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
    BaseHistogram histogram(kBuckets);

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
    BaseHistogram histogram(kBuckets);

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
    BaseHistogram* histogram;

    // Used for signaling the worker thread to start incrementing.
    sync_completion_t* start;

    // Number of times to call Increment.
    size_t operations = 0;
};

// Increment each bucket by 2* operations * bucket_index.
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

// Verifies that calling increment from multiple threads, yields consistent results.
// Multiple threads will call Increment a known number of times, then the total count
// per bucket should be sum of the times each thread called Increment one each bucket.
bool TestIncrementMultiThread() {
    BEGIN_TEST;
    sync_completion_t start;
    BaseHistogram histogram(kBuckets);
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

    // Each thread increses each bucket by 2 * bucket_index, so the expected amount for each bucket
    // is: 2 * bucket_index * Sum(i=0, kThreads -1) i = 2 * bucket_index * kThreads* (kThreads - 1)
    // / 2;
    constexpr size_t amount = (kThreads - 1) * (kThreads);
    for (uint32_t i = 0; i < kBuckets; ++i) {
        // We take the sum of the accumulated and what is left, because the last increment may have
        // been scheduled after the last flush.
        EXPECT_EQ(histogram.GetCount(i), i * amount);
    }
    END_TEST;
}

// Verifies that when flushing an histogram, all the flushed data matches that of the
// count in the histogram.
bool TestFlush() {
    BEGIN_TEST;
    RemoteHistogram histogram = MakeRemoteHistogram();
    fidl::VectorView<HistogramBucket> flushed_event_data;
    uint64_t flushed_metric_id;
    RemoteHistogram::FlushCompleteFn complete_fn;
    const fbl::Vector<Metadata>* flushed_metadata;

    // Increase the count of each bucket bucket_index times.
    for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
        ASSERT_EQ(histogram.GetCount(bucket_index), 0);
        histogram.IncrementCount(bucket_index, bucket_index);
        ASSERT_EQ(histogram.GetCount(bucket_index), bucket_index);
    }

    ASSERT_TRUE(histogram.Flush(
        [&flushed_event_data, &flushed_metadata, &flushed_metric_id, &complete_fn](
            uint64_t metric_id, const EventBuffer<fidl::VectorView<HistogramBucket>>& buffer,
            RemoteHistogram::FlushCompleteFn comp_fn) {
            flushed_event_data = buffer.event_data();
            flushed_metadata = &buffer.metadata();
            flushed_metric_id = metric_id;
            complete_fn = fbl::move(comp_fn);
        }));

    // Check that flushed data is actually what we expect:
    // The metadata is the same, and each bucket contains bucket_index count.
    ASSERT_EQ(flushed_metric_id, kMetricId);
    for (uint64_t metadata_index = 0; metadata_index < GetMetadata().size(); ++metadata_index) {
        EXPECT_TRUE(MetadataEq(*flushed_metadata, GetMetadata()));
    }

    fbl::Vector<HistogramBucket> buckets;
    buckets.reserve(kBuckets);
    for (size_t i = 0; i < kBuckets; ++i) {
        buckets.push_back({.index = static_cast<uint32_t>(i), .count = i});
    }
    fidl::VectorView<HistogramBucket> expected_buckets;
    expected_buckets.set_data(buckets.get());
    expected_buckets.set_count(buckets.size());

    // Verify there is a bucket event_data.
    EXPECT_TRUE(HistEventValuesEq(flushed_event_data, expected_buckets));

    // Until complete_fn is called this should be false.
    ASSERT_FALSE(histogram.Flush(RemoteHistogram::FlushFn()));

    complete_fn();

    // Verify all buckets are 0.
    for (uint32_t bucket_index = 0; bucket_index < kBuckets; ++bucket_index) {
        ASSERT_EQ(histogram.GetCount(bucket_index), 0);
    }

    // Check that after calling complete_fn we can call flush again.
    ASSERT_TRUE(histogram.Flush([](uint64_t metric_id,
                                   const EventBuffer<fidl::VectorView<HistogramBucket>>& values,
                                   RemoteHistogram::FlushCompleteFn comp_fn) {}));

    END_TEST;
}

struct FlushArgs {
    // Pointer to the histogram which is flushing incremental snapshot to a 'remote' histogram.
    RemoteHistogram* histogram;

    // Pointer to the 'Remote' histogram that is accumulating the data of each flush.
    BaseHistogram* accumulated_histogram;

    // Used to enforce the threads start together. The main thread will signal after
    // all threads have been started.
    sync_completion_t* start;

    // Number of times to perform the given operation.
    size_t operations = 0;

    // Whether the thread will flush, if flase it will be incrementing the buckets.
    bool flush = false;
};

int FlushFn(void* args) {
    FlushArgs* flush_args = static_cast<FlushArgs*>(args);

    sync_completion_wait(flush_args->start, zx::sec(20).get());

    for (size_t i = 0; i < flush_args->operations; ++i) {
        if (flush_args->flush) {
            flush_args->histogram->Flush(
                [&flush_args](uint64_t metric_id,
                              const EventBuffer<fidl::VectorView<HistogramBucket>>& buffer,
                              RemoteHistogram::FlushCompleteFn complete_fn) {
                    uint64_t count = buffer.event_data().count();
                    for (uint32_t i = 0; i < count; ++i) {
                        flush_args->accumulated_histogram->IncrementCount(
                            buffer.event_data()[i].index, buffer.event_data()[i].count);
                    }
                });
        } else {
            for (uint32_t j = 0; j < kBuckets; ++j) {
                flush_args->histogram->IncrementCount(j, j);
            }
        }
    }
    return thrd_success;
}

// Verify that under concurrent environment the final results are consistent. This test
// will have |kThreads|/2 threads increment bucket counts, and |kThreads|/2 Flush them
// a certain amount of times, and collect into a BaseHistogram the final results. At the end,
// each bucket of the BaseHistogram should be the expected value.
bool TestFlushMultithread() {
    BEGIN_TEST;
    sync_completion_t start;
    BaseHistogram accumulated(kBuckets);
    RemoteHistogram histogram = MakeRemoteHistogram();
    fbl::Vector<thrd_t> thread_ids;
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
        ASSERT_EQ(thrd_create(&thread_id, FlushFn, &args[i]), thrd_success);
    }

    // Notify threads to start incrementing the count.
    sync_completion_signal(&start);

    // Wait for all threads to finish.
    for (const auto& thread_id : thread_ids) {
        thrd_join(thread_id, nullptr);
    }

    // Each thread at an even position, increases the the count of a bucket by bucket_index.
    constexpr size_t ceil_threads = ((kThreads - 1) / 2 * ((kThreads - 1) / 2 + 1));
    for (uint32_t i = 0; i < kBuckets; ++i) {
        // We take the sum of the accumulated and what is left, because the last increment may have
        // been scheduled after the last flush.
        EXPECT_EQ(accumulated.GetCount(i) + histogram.GetCount(i), i * ceil_threads);
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
END_TEST_CASE(RemoteHistogramTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
