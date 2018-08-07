// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <cobalt-client/cpp/types-internal.h>
#include <fbl/string.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Encoder Id used for setting up metrics parts in this test.
constexpr uint32_t kEncodingId = 20;

// Name used for metric returned by ObservationBuffer->GetMetric()
constexpr char kPartName[] = "SomeName";

// Name used for metric returned by ObservationBuffer->GetMetric()
constexpr char kMetricName[] = "SomeMetricName";

// Number of threads to spawn on multi threaded test.
constexpr size_t kThreads = 20;

bool TestIntValue() {
    BEGIN_TEST;
    Value int_val = IntValue(32);
    ASSERT_EQ(int_val.tag, fuchsia_cobalt_ValueTagint_value);
    ASSERT_EQ(int_val.int_value, 32);
    END_TEST;
}

bool TestDoubleValue() {
    BEGIN_TEST;
    Value dbl_val = DoubleValue(1e-8);
    ASSERT_EQ(dbl_val.tag, fuchsia_cobalt_ValueTagdouble_value);
    ASSERT_EQ(dbl_val.double_value, 1e-8);
    END_TEST;
}

bool TestIndexValue() {
    BEGIN_TEST;
    Value index_val = IndexValue(32);
    ASSERT_EQ(index_val.tag, fuchsia_cobalt_ValueTagindex_value);
    ASSERT_EQ(index_val.index_value, 32);
    END_TEST;
}

bool TestBucketDistributionValue() {
    BEGIN_TEST;
    BucketDistributionEntry entries[5];
    Value buckets_val = BucketDistributionValue(5, entries);
    ASSERT_EQ(buckets_val.tag, fuchsia_cobalt_ValueTagint_bucket_distribution);
    ASSERT_EQ(buckets_val.int_bucket_distribution.count, 5);
    ASSERT_EQ(buckets_val.int_bucket_distribution.data, entries);
    END_TEST;
}

ObservationValue MakeObservation(const char* name, Value value) {
    ObservationValue obs;
    obs.name.size = strlen(name) + 1;
    obs.name.data = const_cast<char*>(name);
    obs.value = value;
    obs.encoding_id = kEncodingId;
    return obs;
}

// Returns a buffer with two metric parts as metadata and |kPartName| as name, and both int values 2
// and 3 respectively.
ObservationBuffer MakeBuffer() {
    fbl::Vector<ObservationValue> metadata = {MakeObservation(kPartName, IntValue(2)),
                                              MakeObservation(kPartName, IntValue(3))};
    ObservationBuffer buffer(metadata);
    *buffer.GetMutableMetric() = MakeObservation(kMetricName, IntValue(32));
    return buffer;
}

// Verify that the metadata is stored correctly.
bool TestMetadataPreserved() {
    BEGIN_TEST;
    ObservationBuffer buffer = MakeBuffer();
    auto data = buffer.GetView();

    ASSERT_EQ(data.count(), 3);

    EXPECT_EQ(data[0].encoding_id, kEncodingId);
    ASSERT_EQ(data[0].name.size, fbl::constexpr_strlen(kPartName) + 1);
    EXPECT_STR_EQ(data[0].name.data, kPartName);
    EXPECT_EQ(data[0].value.int_value, 2);

    EXPECT_EQ(data[1].encoding_id, kEncodingId);
    ASSERT_EQ(data[1].name.size, fbl::constexpr_strlen(kPartName) + 1);
    EXPECT_STR_EQ(data[1].name.data, kPartName);
    EXPECT_EQ(data[1].value.int_value, 3);

    END_TEST;
}

// Verify that changes on GetMetric are persisted.
bool TestMetricUpdatePersisted() {
    BEGIN_TEST;
    ObservationBuffer buffer = MakeBuffer();
    auto data = buffer.GetView();

    ASSERT_EQ(data.count(), 3);

    EXPECT_EQ(data[2].encoding_id, kEncodingId);
    ASSERT_EQ(data[2].name.size, fbl::constexpr_strlen(kMetricName) + 1);
    EXPECT_STR_EQ(data[2].name.data, kMetricName);

    buffer.GetMutableMetric()->value.int_value = 4;
    EXPECT_EQ(data[2].value.int_value, 4);

    buffer.GetMutableMetric()->value.int_value = 20;
    EXPECT_EQ(data[2].value.int_value, 20);

    END_TEST;
}

// Verify while flushing is ongoing no calls to TryBeginFlush returns true.
bool TestFlushDoNotOverlap() {
    BEGIN_TEST;
    ObservationBuffer buffer = MakeBuffer();

    ASSERT_TRUE(buffer.TryBeginFlush());
    ASSERT_FALSE(buffer.TryBeginFlush());

    buffer.CompleteFlush();
    ASSERT_TRUE(buffer.TryBeginFlush());
    END_TEST;
}

struct TryFlushArgs {
    // List of thrd_t that are attempting to flush.
    fbl::Vector<thrd_t>* thread_ids;

    // Counts how many success flushes have ocurred.
    fbl::atomic<uint32_t>* successfull_flushes;

    // Sigal by the main thread so threads start trying to flush.
    sync_completion_t* start;

    // Sigal by the main thread so threads start trying to flush.
    sync_completion_t* done;

    // thrd_t of the thread that actually flushed.
    thrd_t* flushing_thread;

    // Buffer being attempted to flush.
    ObservationBuffer* buffer;
};

int TryFlushFn(void* args) {
    TryFlushArgs* try_flush = static_cast<TryFlushArgs*>(args);
    sync_completion_wait(try_flush->start, zx::sec(20).get());
    if (try_flush->buffer->TryBeginFlush()) {
        // Now we wait for all other threads finish, so
        // the flushing operation is long enough, that we can
        // verify that only a single thread will flush,
        for (auto thread_id : *try_flush->thread_ids) {
            if (thread_id == thrd_current()) {
                continue;
            }
            thrd_join(thread_id, nullptr);
        }
        *try_flush->flushing_thread = thrd_current();
        try_flush->buffer->CompleteFlush();
        try_flush->successfull_flushes->fetch_add(1, fbl::memory_order_relaxed);
        sync_completion_signal(try_flush->done);
    }
    return thrd_success;
};

// With multiple threads attempting to flush, should be flushed a single time.
bool TestSingleFlushWithMultipleThreads() {
    BEGIN_TEST;
    ObservationBuffer buffer = MakeBuffer();
    sync_completion_t start;
    sync_completion_t done;
    fbl::Vector<thrd_t> thread_ids;
    fbl::atomic<uint32_t> successfull_flushes(0);
    thrd_t flushing_thread;
    TryFlushArgs args = {.thread_ids = &thread_ids,
                         .successfull_flushes = &successfull_flushes,
                         .start = &start,
                         .done = &done,
                         .flushing_thread = &flushing_thread,
                         .buffer = &buffer};

    for (size_t thread = 0; thread < kThreads; ++thread) {
        thread_ids.push_back({});
    }

    for (auto& thread_id : thread_ids) {
        ASSERT_EQ(thrd_create(&thread_id, TryFlushFn, &args), thrd_success);
    }

    // Signal threads to start.
    sync_completion_signal(&start);
    sync_completion_wait(&done, zx::sec(20).get());

    ASSERT_EQ(thrd_join(flushing_thread, nullptr), thrd_success);

    // Exactly one flush while a flush operations is on going and multiple threads
    // attempt to flush.
    ASSERT_EQ(successfull_flushes.load(fbl::memory_order_relaxed), 1);

    // Verify completion of the flush operation.
    ASSERT_TRUE(buffer.TryBeginFlush());
    END_TEST;
}

BEGIN_TEST_CASE(ObservationTest)
RUN_TEST(TestIntValue)
RUN_TEST(TestDoubleValue)
RUN_TEST(TestIndexValue)
RUN_TEST(TestBucketDistributionValue)
END_TEST_CASE(ObservationTest)

BEGIN_TEST_CASE(ObservationBufferTest)
RUN_TEST(TestMetadataPreserved)
RUN_TEST(TestMetricUpdatePersisted)
RUN_TEST(TestFlushDoNotOverlap)
RUN_TEST(TestSingleFlushWithMultipleThreads)
END_TEST_CASE(ObservationBufferTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
