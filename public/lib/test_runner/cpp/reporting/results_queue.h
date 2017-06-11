// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_RESULTS_QUEUE_H_
#define APPS_TEST_RUNNER_LIB_RESULTS_QUEUE_H_

#include <magenta/compiler.h>
#include <mx/event.h>
#include <queue>

#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/ftl/synchronization/mutex.h"

namespace test_runner {

// For passing TestResult objects between threads. Uses an mx::event for
// signaling when the queue has items to consume, which allows use with
// mtl::MessageLoop on the consumer side.
class ResultsQueue {
 public:
  static const mx_signals_t kSignal = MX_USER_SIGNAL_0;

  ResultsQueue();

  ~ResultsQueue();

  mx::event* event() {
    return &event_;
  }

  bool Empty();

  void Push(TestResultPtr test_result);

  TestResultPtr Pop();

 private:
  ftl::Mutex mutex_;
  mx::event event_;
  std::queue<TestResultPtr> queue_ __TA_GUARDED(mutex_);
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_RESULTS_QUEUE_H_
