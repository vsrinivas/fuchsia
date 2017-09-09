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

    puts("Doing work for 30 seconds...");

    mx_time_t start_time = now();
    mx_time_t quit_time = start_time + MX_SEC(30);
    async::Task task(start_time);
    task.set_handler([&task, &loop, quit_time](async_t* async, mx_status_t status) {
        TRACE_DURATION("example", "Doing Work!", "async", async, "status", status);

        // Simulate some kind of workload.
        puts("Doing work!");
        mx_nanosleep(now() + MX_MSEC(500));

        // Stop if quitting.
        if (task.deadline() > quit_time) {
            loop.Quit();
            return ASYNC_TASK_FINISHED;
        }

        // Schedule more work in a little bit.
        task.set_deadline(now() + MX_MSEC(200));
        return ASYNC_TASK_REPEAT;
    });

    task.Post(loop.async());

    loop.Run();

    puts("Finished.");
    return 0;
}
