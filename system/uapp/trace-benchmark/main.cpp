// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <stdio.h>

#include <zircon/assert.h>

#include <fbl/array.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <trace/handler.h>

#include "benchmarks.h"

namespace {

// Trace buffer size.
// Should be sized so it does not overflow during the test.
static constexpr size_t kBufferSizeBytes = 16 * 1024 * 1024;

class BenchmarkHandler : public trace::TraceHandler {
public:
    BenchmarkHandler(async::Loop* loop)
        : loop_(loop), buffer_(new uint8_t[kBufferSizeBytes], kBufferSizeBytes) {
    }

    void Start() {
        zx_status_t status = trace_start_engine(loop_->async(), this,
                                                buffer_.get(), buffer_.size());
        ZX_DEBUG_ASSERT(status == ZX_OK);

        puts("\nTrace started\n");
    }

private:
    bool IsCategoryEnabled(const char* category) override {
        // Any category beginning with "+" is enabled.
        return category[0] == '+';
    }

    void TraceStopped(async_t* async,
                      zx_status_t disposition,
                      size_t buffer_bytes_written) override {
        puts("\nTrace stopped");

        ZX_DEBUG_ASSERT(disposition == ZX_OK);
        loop_->Quit();
    }

    async::Loop* loop_;
    fbl::Array<uint8_t> buffer_;
};

} // namespace

int main(int argc, char** argv) {
    async::Loop loop;
    BenchmarkHandler handler(&loop);

    RunTracingDisabledBenchmarks();
    handler.Start();

    async::PostTask(loop.async(), []() {
        RunTracingEnabledBenchmarks();
        RunNoTraceBenchmarks();

        trace_stop_engine(ZX_OK);
    });

    loop.Run(); // run until quit
    return 0;
}
