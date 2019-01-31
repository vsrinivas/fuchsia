// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <zircon/assert.h>
#include <zircon/status.h>

#include <fbl/array.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/event.h>
#include <trace-provider/handler.h>

class BenchmarkHandler : public trace::TraceHandler {
public:
    static constexpr int kWaitStoppedTimeoutSeconds = 10;

    BenchmarkHandler(async::Loop* loop, trace_buffering_mode_t mode,
                     size_t buffer_size)
        : loop_(loop),
          mode_(mode),
          buffer_(new uint8_t[buffer_size], buffer_size) {
        auto status = zx::event::create(0u, &observer_event_);
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                            "zx::event::create returned %s\n",
                            zx_status_get_string(status));
        status = trace_register_observer(observer_event_.get());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                            "trace_register_observer returned %s\n",
                            zx_status_get_string(status));
    }

    ~BenchmarkHandler() {
        auto status = trace_unregister_observer(observer_event_.get());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                            "trace_unregister_observer returned %s\n",
                            zx_status_get_string(status));
    }

    trace_buffering_mode_t mode() const { return mode_; }

    void Start() {
        zx_status_t status = trace_start_engine(loop_->dispatcher(),
                                                this, mode_,
                                                buffer_.get(), buffer_.size());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                            "trace_start_engine returned %s\n",
                            zx_status_get_string(status));
        ZX_DEBUG_ASSERT(trace_state() == TRACE_STARTED);
        observer_event_.signal(ZX_EVENT_SIGNALED, 0u);
        trace_notify_observer_updated(observer_event_.get());
    }

    void Stop() {
        // Acquire the context before we stop. We can't after we stop
        // as the context has likely been released (no more
        // references).
        trace::internal::trace_buffer_header header;
        {
            auto context = trace::TraceProlongedContext::Acquire();
            auto status = trace_stop_engine(ZX_OK);
            ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                                "trace_stop_engine returned %s\n",
                                zx_status_get_string(status));
            trace_context_snapshot_buffer_header(context.get(), &header);
        }

        // Tracing hasn't actually stopped yet. It's stopping, but that won't
        // complete until all context references are gone (which they are),
        // and the engine has processed that fact (which it hasn't necessarily
        // yet).
        while (trace_state() != TRACE_STOPPED) {
            auto status = observer_event_.wait_one(
                ZX_EVENT_SIGNALED,
                zx::deadline_after(zx::sec(kWaitStoppedTimeoutSeconds)),
                nullptr);
            ZX_DEBUG_ASSERT_MSG(status == ZX_OK,
                                "observer_event_.wait_one returned %s\n",
                                zx_status_get_string(status));
            observer_event_.signal(ZX_EVENT_SIGNALED, 0u);
        }

        if (mode_ == TRACE_BUFFERING_MODE_ONESHOT) {
            ZX_DEBUG_ASSERT(header.wrapped_count == 0);
        }
    }

private:
    bool IsCategoryEnabled(const char* category) override {
        // Any category beginning with "+" is enabled.
        return category[0] == '+';
    }

    void TraceStopped(async_dispatcher_t* async,
                      zx_status_t disposition,
                      size_t buffer_bytes_written) override {
        // This is noise if the status is ZX_OK, so just print if error.
        // There's also no point in printing for ZX_ERR_NO_MEMORY, as that
        // information can be determined from the number of records dropped.
        if  (disposition != ZX_OK && disposition != ZX_ERR_NO_MEMORY) {
            printf("WARNING: Trace stopped, disposition = %s\n",
                   zx_status_get_string(disposition));
        }

        if (mode_ == TRACE_BUFFERING_MODE_STREAMING) {
            ZX_DEBUG_ASSERT(disposition == ZX_OK ||
                            // Some records could have been dropped while
                            // "saving" the buffer.
                            disposition == ZX_ERR_NO_MEMORY);
        } else {
            // In oneshot and circular modes we shouldn't have dropped
            // any records.
            ZX_DEBUG_ASSERT(disposition == ZX_OK);
        }
    }

    void NotifyBufferFull(uint32_t wrapped_count,
                          uint64_t durable_data_end) override {
        // We shouldn't get this in oneshot or circular modes.
        ZX_DEBUG_ASSERT(mode_ == TRACE_BUFFERING_MODE_STREAMING);

        // The intent isn't to include buffer-save time in the benchmarks,
        // so just immediately flag the buffer as saved. Alas since we're
        // running on a separate thread records may get dropped. It depends on
        // how well we're scheduled.
        trace_engine_mark_buffer_saved(wrapped_count, durable_data_end);
    }

    async::Loop* const loop_;
    const trace_buffering_mode_t mode_;
    fbl::Array<uint8_t> const buffer_;
    zx::event observer_event_;
};
