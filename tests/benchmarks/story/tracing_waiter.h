// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_BENCHMARKS_STORY_TRACING_WAITER_H_
#define PERIDOT_TESTS_BENCHMARKS_STORY_TRACING_WAITER_H_

#include <functional>
#include <memory>

#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

namespace modular {

// An instance of this class can be used to wait for the tracing system to be
// ready to use. A client calls WaitForTracing() on an instance of this class,
// and is free to make tracing calls once the callback is invoked.
class TracingWaiter {
 public:
  TracingWaiter();
  ~TracingWaiter();

  void WaitForTracing(std::function<void()>);

 private:
  bool started_{};
  std::unique_ptr<trace::TraceProvider> trace_provider_;
  std::unique_ptr<trace::TraceObserver> trace_observer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TracingWaiter);
};

}  // namespace modular

#endif  // PERIDOT_TESTS_BENCHMARKS_STORY_TRACING_WAITER_H_
