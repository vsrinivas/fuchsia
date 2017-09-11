// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_TASKS_ONE_SHOT_TIMER_H_
#define LIB_FXL_TASKS_ONE_SHOT_TIMER_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"

namespace fxl {

// Posts tasks to a |TaskRunner| to run once after a delay.
// It may only be used on the same thread as the task runner.
class FXL_EXPORT OneShotTimer {
 public:
  OneShotTimer();
  ~OneShotTimer();

  // Returns true if the timer was started and has not expired or been stopped.
  bool is_started() const { return !!task_; }

  // Posts |task| to |task_runner| to run after the given |delay| unless
  // the timer is stopped before the task runs.
  void Start(TaskRunner* task_runner, const Closure& task, TimeDelta delay);

  // Stops the timer.
  // Does nothing if not started.
  void Stop();

 private:
  void RunTask();

  Closure task_;
  WeakPtrFactory<OneShotTimer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OneShotTimer);
};

}  // namespace fxl

#endif  // LIB_FXL_TASKS_ONE_SHOT_TIMER_H_
