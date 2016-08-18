// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TASKS_ONE_SHOT_TIMER_H_
#define LIB_FTL_TASKS_ONE_SHOT_TIMER_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"

namespace ftl {

// Posts tasks to a |TaskRunner| to run once after a delay.
// It may only be used on the same thread as the task runner.
class OneShotTimer {
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

  FTL_DISALLOW_COPY_AND_ASSIGN(OneShotTimer);
};

}  // namespace ftl

#endif  // LIB_FTL_TASKS_ONE_SHOT_TIMER_H_
