// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/reporting/results_queue.h"

#include <mx/event.h>

#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/ftl/synchronization/mutex.h"

namespace test_runner {

ResultsQueue::ResultsQueue() {
  mx::event::create(0, &event_);
}

ResultsQueue::~ResultsQueue() {}

bool ResultsQueue::Empty() {
  ftl::MutexLocker locker(&mutex_);
  return queue_.empty();
}

void ResultsQueue::Push(TestResultPtr test_result) {
  ftl::MutexLocker locker(&mutex_);
  queue_.push(std::move(test_result));
  event_.signal(0, kSignal);
}

TestResultPtr ResultsQueue::Pop() {
  ftl::MutexLocker locker(&mutex_);
  TestResultPtr result = std::move(queue_.front());
  queue_.pop();
  if (queue_.empty()) {
    event_.signal(kSignal, 0);
  }
  return result;
}

}  // namespace test_runner
