// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_BENCHMARK_STORY_TRACING_BASE_H_
#define PERIDOT_TESTS_BENCHMARK_STORY_TRACING_BASE_H_

#include <functional>
#include <memory>

#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

#include "lib/fxl/macros.h"

namespace modular {

class TracingBase {
 protected:
  void WaitForTracing(std::function<void()>);

 private:
  bool started_{};
  std::unique_ptr<trace::TraceProvider> trace_provider_;
  std::unique_ptr<trace::TraceObserver> trace_observer_;
};

}  // namespace modular

#endif  // PERIDOT_TESTS_BENCHMARK_STORY_TRACING_BASE_H_
