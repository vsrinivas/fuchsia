// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <stdio.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

int main(int argc, char** argv) {
    async::Loop loop;
    trace::TraceProvider provider(loop.async());

    puts("Doing work for 30 seconds...");

    zx::time start_time = async::Now(loop.async());
    zx::time quit_time = start_time + zx::sec(30);

    async::Task task([&loop, quit_time](async_t* async, async::Task* task, zx_status_t status) {
        TRACE_DURATION("example", "Doing Work!", "async", async, "status", status);
        if (status != ZX_OK)
            return;

        // Simulate some kind of workload.
        puts("Doing work!");
        zx::nanosleep(zx::deadline_after(zx::msec(500)));

        // Stop if quitting.
        zx::time now = async::Now(loop.async());
        if (now > quit_time) {
            loop.Quit();
            return;
        }

        // Schedule more work in a little bit.
        task->PostForTime(async, now + zx::msec(200));
    });
    task.PostForTime(loop.async(), start_time);

    loop.Run();

    puts("Finished.");
    return 0;
}
