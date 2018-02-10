// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/tests/clock_task_runner.h"

namespace mz {
namespace test {

fxl::RefPtr<ClockTaskRunner> ClockTaskRunner::New(zx_time_t initial_nanos) {
  return fxl::AdoptRef(new ClockTaskRunner(initial_nanos));
}

ClockTaskRunner::ClockTaskRunner(zx_time_t initial_nanos)
    : nanos_(initial_nanos) {}

ClockTaskRunner::~ClockTaskRunner() = default;

void ClockTaskRunner::PostTask(fxl::Closure task) {
  runnable_tasks_.push(std::move(task));
}

void ClockTaskRunner::PostTaskForTime(fxl::Closure task,
                                      fxl::TimePoint target_time) {
  PostTaskForNanoseconds(std::move(task),
                         target_time.ToEpochDelta().ToNanoseconds());
}

void ClockTaskRunner::PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) {
  PostTaskForNanoseconds(std::move(task), nanos_ + delay.ToNanoseconds());
}

void ClockTaskRunner::PostTaskForNanoseconds(fxl::Closure task,
                                             zx_time_t nanos) {
  if (nanos <= nanos_) {
    // Task can run immediately.
    runnable_tasks_.push(std::move(task));
  } else {
    // Task must wait before running.
    delayed_tasks_[nanos].push_back(std::move(task));
  }
}

bool ClockTaskRunner::RunsTasksOnCurrentThread() {
  return true;
}

void ClockTaskRunner::Tick(zx_time_t delta_nanos) {
  FXL_CHECK(!ticking_);
  FXL_CHECK(delta_nanos >= 0);
  ticking_ = true;
  zx_time_t final_nanos = nanos_ + delta_nanos;

  RunRunnableTasks();

  auto it = delayed_tasks_.begin();
  while (it != delayed_tasks_.end()) {
    if (it->first > final_nanos) {
      // All remaining tasks must wait before running.
      break;
    }

    // Update the current time, move the current batch of delayed tasks to the
    // runnable queue, and run them.
    FXL_CHECK(it->first > nanos_);
    nanos_ = it->first;
    for (auto& task : it->second) {
      runnable_tasks_.push(std::move(task));
    }
    delayed_tasks_.erase(it);
    RunRunnableTasks();

    // Prepare to run the next batch of delayed tasks.
    it = delayed_tasks_.begin();
  }

  // Update the clock to the final time.
  nanos_ = final_nanos;
  ticking_ = false;
}

void ClockTaskRunner::RunRunnableTasks() {
  FXL_CHECK(ticking_);

  while (!runnable_tasks_.empty()) {
    fxl::Closure task(std::move(runnable_tasks_.front()));
    runnable_tasks_.pop();
    task();
  }
}

}  // namespace test
}  // namespace mz
