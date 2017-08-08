// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include <stdio.h>

#include <async/loop.h>
#include <async/task.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

namespace {

mx_time_t now() {
    return mx_time_get(MX_CLOCK_MONOTONIC);
}

} // namespace

int main(int argc, char** argv) {
    async::Loop loop;
    trace::TraceProvider provider(loop.async());

    puts("Starting Benchmark...");

    mx_time_t start_time = now();
    mx_time_t quit_time = start_time + MX_SEC(2);
    async::Task task(start_time);
    // This async task runs for two seconds until `quit_time` is reached.
    task.set_handler([&task, &loop, quit_time](async_t* async, mx_status_t status) {
        // `task_start` and `task_end` are used to measure the time between
        // `example` benchmarks.  This is measured with a `time_between`
        // measurement type.
        TRACE_INSTANT("benchmark", "task_start", TRACE_SCOPE_PROCESS);

        // An `example` benchmark measured with a `duration` measurement type.
        TRACE_DURATION("benchmark", "example");

        // Simulate some kind of workload.
        mx_nanosleep(now() + MX_MSEC(10));

        // Stop if quitting.
        if (task.deadline() > quit_time) {
            loop.Quit();
            return ASYNC_TASK_FINISHED;
        }

        // Schedule another benchmark.
        task.set_deadline(now() + MX_MSEC(5));
        TRACE_INSTANT("benchmark", "task_end", TRACE_SCOPE_PROCESS);
        return ASYNC_TASK_REPEAT;
    });

    task.Post(loop.async());

    loop.Run();

    puts("Finished.");
    return 0;
}
