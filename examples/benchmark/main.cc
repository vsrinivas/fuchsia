// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <stdio.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <zx/time.h>

int main(int argc, char** argv) {
  async::Loop loop;
  trace::TraceProvider provider(loop.dispatcher());

  // Wait for tracing to get set up.  This works around a race condition in
  // the tracing system (see TO-650).  Without this, the tracing system can
  // miss some of the initial tracing events we generate later.
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
    // `example` benchmarks.  This is measured with a `time_between`
    // measurement type.
    TRACE_INSTANT("benchmark", "task_start", TRACE_SCOPE_PROCESS);

    // An `example` benchmark measured with a `duration` measurement type.
    TRACE_DURATION("benchmark", "example");

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
