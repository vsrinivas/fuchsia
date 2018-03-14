// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_CLOCK_TASK_RUNNER_H_
#define GARNET_LIB_UI_SCENIC_TESTS_CLOCK_TASK_RUNNER_H_

#include <map>
#include <queue>
#include <vector>

#include "garnet/lib/ui/scenic/clock.h"
#include "lib/fxl/tasks/task_runner.h"

namespace mz {
namespace test {

// Implements both Clock and TaskRunner.
class ClockTaskRunner : public fxl::TaskRunner, public Clock {
 public:
  static fxl::RefPtr<ClockTaskRunner> New(zx_time_t initial_nanos);

  // Increment the clock by the specified number of nanoseconds, and run any
  // tasks whose run-time has been reached.
  void Tick(zx_time_t delta_nanos);

  // |fxl::TaskRunner|
  void PostTask(fxl::Closure task) override;

  // |fxl::TaskRunner|
  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) override;

  // |fxl::TaskRunner|
  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) override;

  // |fxl::TaskRunner|
  bool RunsTasksOnCurrentThread() override;

  // |Clock|
  zx_time_t GetNanos() override { return nanos_; }

 private:
  explicit ClockTaskRunner(zx_time_t initial_nanos);
  ~ClockTaskRunner() override;

  void PostTaskForNanoseconds(fxl::Closure task, zx_time_t nanos);
  void RunRunnableTasks();

  zx_time_t nanos_;
  bool ticking_ = false;

  std::queue<fxl::Closure> runnable_tasks_;
  std::map<zx_time_t, std::vector<fxl::Closure>> delayed_tasks_;

  FRIEND_REF_COUNTED_THREAD_SAFE(ClockTaskRunner);
  FXL_DISALLOW_COPY_AND_ASSIGN(ClockTaskRunner);
};

}  // namespace test
}  // namespace mz

#endif  // GARNET_LIB_UI_SCENIC_TESTS_CLOCK_TASK_RUNNER_H_
