// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <zircon/assert.h>
#include <zircon/status.h>

#include <fbl/array.h>
#include <lib/async-loop/cpp/loop.h>

#include <trace/handler.h>

class BenchmarkHandler : public trace::TraceHandler {
public:
    BenchmarkHandler(async::Loop* loop, const char* name,
                     trace_buffering_mode_t mode, size_t buffer_size)
        : loop_(loop),
          name_(name),
          mode_(mode),
          buffer_(new uint8_t[buffer_size], buffer_size) {
    }

    trace_buffering_mode_t mode() const { return mode_; }

    void Start() {
        zx_status_t status = trace_start_engine(loop_->dispatcher(),
                                                this, mode_,
                                                buffer_.get(), buffer_.size());
        ZX_DEBUG_ASSERT(status == ZX_OK);

        printf("\nTrace with benchmark spec \"%s\" started\n", name_);
    }

private:
    bool IsCategoryEnabled(const char* category) override {
        // Any category beginning with "+" is enabled.
        return category[0] == '+';
    }

    void TraceStopped(async_dispatcher_t* async,
                      zx_status_t disposition,
                      size_t buffer_bytes_written) override {
        printf("Trace stopped, disposition = %s\n",
               zx_status_get_string(disposition));

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

        loop_->Quit();
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
    const char* const name_;
    const trace_buffering_mode_t mode_;
    fbl::Array<uint8_t> const buffer_;
};
