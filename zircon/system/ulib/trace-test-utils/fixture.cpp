// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-test-utils/fixture.h"

#include <regex.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <utility>

#include <zircon/assert.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/event.h>
#include <trace-provider/handler.h>
#include <trace-reader/reader.h>
#include <trace-reader/reader_internal.h>
#include <trace-test-utils/compare_records.h>
#include <trace-test-utils/read_records.h>

namespace {

class Fixture : private trace::TraceHandler {
public:
    Fixture(attach_to_thread_t attach_to_thread,
            trace_buffering_mode_t mode, size_t buffer_size)
        : attach_to_thread_(attach_to_thread),
          loop_(attach_to_thread == kAttachToThread
                ? &kAsyncLoopConfigAttachToThread
                : &kAsyncLoopConfigNoAttachToThread),
          buffering_mode_(mode),
          buffer_(new uint8_t[buffer_size], buffer_size) {
        zx_status_t status = zx::event::create(0u, &trace_stopped_);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        status = zx::event::create(0u, &buffer_full_);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        ResetEngineState();
    }

    ~Fixture() {
        StopTracing(false);
    }

    void ResetEngineState() {
        // The engine may be started/stopped multiple times between one call
        // to StartTracing()/StopTracing(). Reset related state tracking vars
        // each time.
        disposition_ = ZX_ERR_INTERNAL;
        buffer_bytes_written_ = 0u;
        observed_stopped_callback_ = false;
        ResetBufferFullNotification();
    }

    void StartEngine() {
        ResetEngineState();

        zx_status_t status = trace_start_engine(loop_.dispatcher(), this,
                                                buffering_mode_,
                                                buffer_.get(), buffer_.size());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }

    void StartTracing() {
        if (trace_running_)
            return;

        trace_running_ = true;

        if (attach_to_thread_ == kNoAttachToThread) {
            loop_.StartThread("trace test");
        }

        StartEngine();
    }

    void StopEngine() {
        ZX_DEBUG_ASSERT(trace_running_);
        zx_status_t status = trace_stop_engine(ZX_OK);
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }

    void WaitEngineStopped() {
        zx_status_t status;
        while (trace_state() != TRACE_STOPPED) {
            if (attach_to_thread_ == kNoAttachToThread) {
                status = trace_stopped_.wait_one(
                    ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(100)),
                    nullptr);
                ZX_DEBUG_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_TIMED_OUT,
                                    "status=%d", status);
            } else {
                // Finish up any remaining tasks. The engine may have queued some.
                status = loop_.RunUntilIdle();
                ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);
            }
        }
    }

    void Shutdown() {
        // Shut down the loop (implicitly joins the thread we started
        // earlier). When this completes we know the trace engine is
        // really stopped.
        loop_.Shutdown();

        ZX_DEBUG_ASSERT(observed_stopped_callback_);

        trace_running_ = false;
    }

    void StopTracing(bool hard_shutdown) {
        if (!trace_running_)
            return;

        // Asynchronously stop the engine.
        // If we're performing a hard shutdown, skip this step and begin immediately
        // tearing down the loop.  The trace engine should stop itself.
        if (!hard_shutdown) {
            StopEngine();
            WaitEngineStopped();
        }

        Shutdown();
    }

    bool WaitBufferFullNotification() {
        auto status = buffer_full_.wait_one(ZX_EVENT_SIGNALED,
                                            zx::deadline_after(zx::msec(1000)), nullptr);
        buffer_full_.signal(ZX_EVENT_SIGNALED, 0u);
        return status == ZX_OK;
    }

    async::Loop& loop() {
        return loop_;
    }

    zx_status_t disposition() const {
        return disposition_;
    }

    bool observed_notify_buffer_full_callback() const {
        return observed_notify_buffer_full_callback_;
    }

    bool observed_buffer_full_wrapped_count() const {
        return observed_buffer_full_wrapped_count_;
    }

    bool observed_buffer_full_durable_data_end() const {
        return observed_buffer_full_durable_data_end_;
    }

    void ResetBufferFullNotification() {
        observed_notify_buffer_full_callback_ = false;
        observed_buffer_full_wrapped_count_ = 0;
        observed_buffer_full_durable_data_end_ = 0;
    }

    bool ReadRecords(fbl::Vector<trace::Record>* out_records) {
        return trace_testing::ReadRecords(buffer_.get(), buffer_.size(),
                                          out_records);
    }

private:
    bool IsCategoryEnabled(const char* category) override {
        // All categories which begin with + are enabled.
        return category[0] == '+';
    }

    void TraceStopped(async_dispatcher_t* dispatcher,
                      zx_status_t disposition,
                      size_t buffer_bytes_written) override {
        ZX_DEBUG_ASSERT(!observed_stopped_callback_);
        observed_stopped_callback_ = true;
        ZX_DEBUG_ASSERT(dispatcher = loop_.dispatcher());
        disposition_ = disposition;
        buffer_bytes_written_ = buffer_bytes_written;

        trace_stopped_.signal(0u, ZX_EVENT_SIGNALED);

        // The normal provider support does "delete this" here.
        // We don't need nor want it as we still have to verify the results.
    }

    void NotifyBufferFull(uint32_t wrapped_count, uint64_t durable_data_end)
            override {
        observed_notify_buffer_full_callback_ = true;
        observed_buffer_full_wrapped_count_ = wrapped_count;
        observed_buffer_full_durable_data_end_ = durable_data_end;
        buffer_full_.signal(0u, ZX_EVENT_SIGNALED);
    }

    attach_to_thread_t attach_to_thread_;
    async::Loop loop_;
    trace_buffering_mode_t buffering_mode_;
    fbl::Array<uint8_t> buffer_;
    bool trace_running_ = false;
    zx_status_t disposition_;
    size_t buffer_bytes_written_;
    zx::event trace_stopped_;
    zx::event buffer_full_;
    bool observed_stopped_callback_;
    bool observed_notify_buffer_full_callback_;
    uint32_t observed_buffer_full_wrapped_count_;
    uint64_t observed_buffer_full_durable_data_end_;
};

Fixture* g_fixture{nullptr};

} // namespace

void fixture_set_up(attach_to_thread_t attach_to_thread,
                    trace_buffering_mode_t mode, size_t buffer_size) {
    ZX_DEBUG_ASSERT(!g_fixture);
    g_fixture = new Fixture(attach_to_thread, mode, buffer_size);
}

void fixture_tear_down(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    delete g_fixture;
    g_fixture = nullptr;
}

void fixture_start_tracing() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StartTracing();
}

void fixture_stop_tracing() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(false);
}

void fixture_stop_tracing_hard() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopTracing(true);
}

void fixture_start_engine() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StartEngine();
}

void fixture_stop_engine() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->StopEngine();
}

void fixture_wait_engine_stopped(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->WaitEngineStopped();
}

void fixture_shutdown() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->Shutdown();
}

async_loop_t* fixture_async_loop(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    return g_fixture->loop().loop();
}

zx_status_t fixture_get_disposition(void) {
    ZX_DEBUG_ASSERT(g_fixture);
    return g_fixture->disposition();
}

bool fixture_wait_buffer_full_notification() {
    ZX_DEBUG_ASSERT(g_fixture);
    return g_fixture->WaitBufferFullNotification();
}

uint32_t fixture_get_buffer_full_wrapped_count() {
    ZX_DEBUG_ASSERT(g_fixture);
    return g_fixture->observed_buffer_full_wrapped_count();
}

void fixture_reset_buffer_full_notification() {
    ZX_DEBUG_ASSERT(g_fixture);
    g_fixture->ResetBufferFullNotification();
}

bool fixture_read_records(fbl::Vector<trace::Record>* out_records) {
    return g_fixture->ReadRecords(out_records);
}

bool fixture_compare_raw_records(const fbl::Vector<trace::Record>& records,
                                 size_t start_record, size_t max_num_records,
                                 const char* expected) {
    return trace_testing::CompareRecords(records, start_record,
                                         max_num_records, expected);
}

bool fixture_compare_n_records(size_t max_num_records, const char* expected,
                               fbl::Vector<trace::Record>* out_records,
                               size_t* out_leading_to_skip) {
    ZX_DEBUG_ASSERT(g_fixture);

    g_fixture->StopTracing(false);

    if (!fixture_read_records(out_records)) {
        return false;
    }

    return trace_testing::ComparePartialBuffer(*out_records,
                                               max_num_records, expected,
                                               out_leading_to_skip);
}

bool fixture_compare_records(const char* expected) {
    fbl::Vector<trace::Record> records;
    return fixture_compare_n_records(SIZE_MAX, expected, &records, nullptr);
}

void fixture_snapshot_buffer_header(trace_buffer_header* header) {
    auto context = trace::TraceProlongedContext::Acquire();
    trace_context_snapshot_buffer_header(context.get(), header);
}
