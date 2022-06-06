// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/syscalls.h>

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  // Wait for tracing to get set up.  Without this, the tracing system can miss
  // some of the initial tracing events we generate later.
  //
  // TODO(fxbug.dev/22911): Replace this sleep with single function that will start
  // a TraceProvider in a non-racy way.
  puts("Sleeping to allow tracing to start...");
  loop.Run(zx::deadline_after(zx::sec(1)));

  puts("Starting Benchmark...");

  // Run the task for kIterationCount iterations.  We use a fixed number
  // of iterations (rather than iterating the test until a fixed amount
  // of time has elapsed) to avoid some statistical problems with using a
  // variable sample size.
  const uint32_t kIterationCount = 1000;
  uint32_t iteration = 0;

  async::TaskClosure task([&loop, &task, &iteration] {
    // `task_start` and `task_end` are used to measure the time between
    // `example_event` benchmarks.  This is measured with a `time_between`
    // measurement type.
    TRACE_INSTANT("benchmark", "task_start", TRACE_SCOPE_PROCESS);

    // An `example_event` benchmark measured with a `duration` measurement
    // type.
    TRACE_DURATION("benchmark", "example_event");

    // Simulate some kind of workload.
    zx::nanosleep(zx::deadline_after(zx::usec(1500)));

    if (++iteration >= kIterationCount) {
      loop.Quit();
      return;
    }

    // Schedule another benchmark.
    TRACE_INSTANT("benchmark", "task_end", TRACE_SCOPE_PROCESS);
    task.PostDelayed(loop.dispatcher(), zx::usec(500));
  });

  task.Post(loop.dispatcher());

  loop.Run();

  puts("Finished.");
  return 0;
}
