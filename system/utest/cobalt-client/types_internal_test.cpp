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

// Number of threads to spawn on multi threaded test.
constexpr size_t kThreads = 20;

// Fixed component name.
constexpr char kComponent[] = "SomeRandomComponent";

// Return a buffer with two event_types each with their own event_type_index.
EventBuffer<uint32_t> MakeBuffer() {
    fbl::Vector<Metadata> metadata = {{.event_type = 1, .event_type_index = 2},
                                      {.event_type = 2, .event_type_index = 4}};
    EventBuffer<uint32_t> buffer(kComponent, metadata);
    *buffer.mutable_event_data() = 0;
    return buffer;
}

// Return a buffer with two event_types each with their own event_type_index.
EventBuffer<uint32_t> MakeBufferWithoutComponent() {
    fbl::Vector<Metadata> metadata = {{.event_type = 1, .event_type_index = 2},
                                      {.event_type = 2, .event_type_index = 4}};
    EventBuffer<uint32_t> buffer(metadata);
    *buffer.mutable_event_data() = 0;
    return buffer;
}

// Verify that the metadata is stored correctly.
bool TestMetadataPreserved() {
    BEGIN_TEST;
    EventBuffer<uint32_t> buffer = MakeBuffer();

    ASSERT_EQ(buffer.metadata().size(), 2);
    ASSERT_EQ(buffer.metadata()[0].event_type, 1);
    ASSERT_EQ(buffer.metadata()[0].event_type_index, 2);
    ASSERT_EQ(buffer.metadata()[1].event_type, 2);
    ASSERT_EQ(buffer.metadata()[1].event_type_index, 4);
    ASSERT_EQ(buffer.event_data(), 0);
    ASSERT_EQ(buffer.component().size(), strlen(kComponent));
    ASSERT_STR_EQ(buffer.component().data(), kComponent);

    END_TEST;
}

// Verify that the metadata is stored correctly.
bool TestMetadataPreservedNoComponent() {
    BEGIN_TEST;
    EventBuffer<uint32_t> buffer = MakeBufferWithoutComponent();

    ASSERT_EQ(buffer.metadata().size(), 2);
    ASSERT_EQ(buffer.metadata()[0].event_type, 1);
    ASSERT_EQ(buffer.metadata()[0].event_type_index, 2);
    ASSERT_EQ(buffer.metadata()[1].event_type, 2);
    ASSERT_EQ(buffer.metadata()[1].event_type_index, 4);
    ASSERT_EQ(buffer.event_data(), 0);
    ASSERT_EQ(buffer.component().size(), 0);
    printf("%p\n", buffer.component().data());
    ASSERT_TRUE(buffer.component().is_null());

    END_TEST;
}

// Verify that changes on GetMetric are persisted.
bool TestMetricUpdatePersisted() {
    BEGIN_TEST;
    EventBuffer<uint32_t> buffer = MakeBuffer();

    ASSERT_EQ(buffer.event_data(), 0);

    *buffer.mutable_event_data() = 4;
    EXPECT_EQ(buffer.event_data(), 4);

    *buffer.mutable_event_data() = 20;
    EXPECT_EQ(buffer.event_data(), 20);

    END_TEST;
}

// Verify while flushing is ongoing no calls to TryBeginFlush returns true.
bool TestFlushDoNotOverlap() {
    BEGIN_TEST;
    EventBuffer<uint32_t> buffer = MakeBuffer();

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
    EventBuffer<uint32_t>* buffer;
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
    EventBuffer<uint32_t> buffer = MakeBuffer();
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

BEGIN_TEST_CASE(EventBufferTest)
RUN_TEST(TestMetadataPreserved)
RUN_TEST(TestMetadataPreservedNoComponent)
RUN_TEST(TestMetricUpdatePersisted)
RUN_TEST(TestFlushDoNotOverlap)
RUN_TEST(TestSingleFlushWithMultipleThreads)
END_TEST_CASE(EventBufferTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
