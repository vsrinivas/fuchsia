// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <cobalt-client/cpp/metric-options.h>
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

// Name of component used for options.
constexpr char kComponent[] = "SomeRandomComponent";

constexpr uint32_t kMetricId = 1;

constexpr uint32_t kEventCode = 2;

// Return a buffer with two event_types each with their own event_type_index.
EventBuffer<uint32_t> MakeBuffer() {
    EventBuffer<uint32_t> buffer;
    *buffer.mutable_event_data() = 0;
    return buffer;
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

const char* GetMetricName(uint32_t metric_id) {
    if (metric_id == kMetricId) {
        return "MetricName";
    }
    return "UnknownMetric";
}

const char* GetEventName(uint32_t event_code) {
    if (event_code == kEventCode) {
        return "EventName";
    }
    return "UnknownEvent";
}

MetricOptions MakeMetricOptions() {
    MetricOptions options;
    options.component = kComponent;
    options.event_code = kEventCode;
    options.metric_id = kMetricId;
    options.get_metric_name = GetMetricName;
    options.get_event_name = GetEventName;
    return options;
}

bool TestFromMetricOptions() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.Both();
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.SomeRandomComponent.EventName");
    END_TEST;
}

bool TestFromMetricOptionsNoGetMetricName() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.Both();
    options.get_metric_name = nullptr;
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "1.SomeRandomComponent.EventName");
    END_TEST;
}

bool TestFromMetricOptionsNoGetEventName() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.Both();
    options.get_event_name = nullptr;
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.SomeRandomComponent.2");
    END_TEST;
}

bool TestFromMetricOptionsNoComponent() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.Both();
    options.component.clear();
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.EventName");
    END_TEST;
}

BEGIN_TEST_CASE(LocalMetricInfo)
RUN_TEST(TestFromMetricOptions)
RUN_TEST(TestFromMetricOptionsNoComponent)
RUN_TEST(TestFromMetricOptionsNoGetMetricName)
RUN_TEST(TestFromMetricOptionsNoGetEventName)
END_TEST_CASE(LocalMetricInfo)

BEGIN_TEST_CASE(EventBufferTest)
RUN_TEST(TestMetricUpdatePersisted)
RUN_TEST(TestFlushDoNotOverlap)
RUN_TEST(TestSingleFlushWithMultipleThreads)
END_TEST_CASE(EventBufferTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
