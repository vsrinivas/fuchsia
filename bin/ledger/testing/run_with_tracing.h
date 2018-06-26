// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_RUN_WITH_TRACING_H_
#define PERIDOT_BIN_LEDGER_TESTING_RUN_WITH_TRACING_H_

#include <functional>

#include <lib/async-loop/cpp/loop.h>

namespace test {
namespace benchmark {

// Adds a TraceObserver to start running |runnable| as soon as the tracing is
// enabled; then runs the message loop |loop|.
// If tracing is still not enabled after 5 seconds, posts a quit task.
int RunWithTracing(async::Loop* loop, std::function<void()> runnable);

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_RUN_WITH_TRACING_H_
