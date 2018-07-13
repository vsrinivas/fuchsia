// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <zircon/assert.h>

#include <fbl/array.h>
#include <lib/async-loop/cpp/loop.h>

#include <trace/handler.h>

class BenchmarkHandler : public trace::TraceHandler {
public:
    BenchmarkHandler(async::Loop* loop, const char* name,
                     size_t buffer_size)
        : loop_(loop),
          name_(name),
          buffer_(new uint8_t[buffer_size], buffer_size) {
    }

    void Start() {
        zx_status_t status = trace_start_engine(loop_->dispatcher(), this,
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
        puts("Trace stopped");

        // In oneshot mode we shouldn't have dropped any records.
        ZX_DEBUG_ASSERT(disposition == ZX_OK);

        loop_->Quit();
    }

    void NotifyBufferFull() override {
        // If we get this in oneshot mode then the buffer wasn't big enough,
        // the benchmarks are defined to run without filling the buffer.
        ZX_DEBUG_ASSERT(false);
    }

    async::Loop* loop_;
    const char* name_;
    fbl::Array<uint8_t> buffer_;
};
