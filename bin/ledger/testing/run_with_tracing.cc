// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/run_with_tracing.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

namespace test {
namespace benchmark {

int RunWithTracing(async::Loop* loop, fit::function<void()> runnable) {
  trace::TraceProvider trace_provider(loop->dispatcher());
  trace::TraceObserver trace_observer;

  bool started = false;
  auto on_trace_state_changed = [runnable = std::move(runnable), &started]() {
    if (TRACE_CATEGORY_ENABLED("benchmark") && !started) {
      started = true;
      runnable();
    }
  };
  // In case tracing has already started.
  on_trace_state_changed();

  if (!started) {
    trace_observer.Start(loop->dispatcher(), std::move(on_trace_state_changed));
  }

  int err = 0;
  async::TaskClosure quit_task([&started, loop, &err] {
    if (!started) {
      // To avoid running the runnable if the tracing state changes to
      // started in the immediate next task on the queue (before the quit
      // task executes).
      started = true;
      FXL_LOG(ERROR)
          << "Timed out waiting for the tracing to start; Did you run the "
             "binary with the trace tool enabled?";
      err = -1;
      loop->Quit();
    }
  });
  quit_task.PostDelayed(loop->dispatcher(), zx::sec(5));

  loop->Run();
  return err;
}

}  // namespace benchmark
}  // namespace test
