// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <stdio.h>

#include <async/loop.h>
#include <async/task.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

namespace {

zx_time_t now() {
    return zx_time_get(ZX_CLOCK_MONOTONIC);
}

} // namespace

int main(int argc, char** argv) {
    async::Loop loop;
    trace::TraceProvider provider(loop.async());

    puts("Starting Benchmark...");

    zx_time_t start_time = now();
    async::Task task(start_time);

    // Run the task for kIterationCount iterations.  We use a fixed number
    // of iterations (rather than iterating the test until a fixed amount
    // of time has elapsed) to avoid some statistical problems with using a
    // variable sample size.
    const uint32_t kIterationCount = 1000;
    uint32_t iteration = 0;

    task.set_handler([&task, &loop, &iteration](async_t* async, zx_status_t status) {
        // `task_start` and `task_end` are used to measure the time between
        // `example` benchmarks.  This is measured with a `time_between`
        // measurement type.
        TRACE_INSTANT("benchmark", "task_start", TRACE_SCOPE_PROCESS);

        // An `example` benchmark measured with a `duration` measurement type.
        TRACE_DURATION("benchmark", "example");

        // Simulate some kind of workload.
        zx_nanosleep(now() + ZX_USEC(1500));

        if (++iteration >= kIterationCount) {
            loop.Quit();
            return ASYNC_TASK_FINISHED;
        }

        // Schedule another benchmark.
        task.set_deadline(now() + ZX_USEC(500));
        TRACE_INSTANT("benchmark", "task_end", TRACE_SCOPE_PROCESS);
        return ASYNC_TASK_REPEAT;
    });

    task.Post(loop.async());

    loop.Run();

    puts("Finished.");
    return 0;
}
