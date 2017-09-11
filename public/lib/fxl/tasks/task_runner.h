// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_TASKS_TASK_RUNNER_H_
#define LIB_FXL_TASKS_TASK_RUNNER_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace fxl {

// Posts tasks to a task queue.
class FXL_EXPORT TaskRunner : public RefCountedThreadSafe<TaskRunner> {
 public:
  // Posts a task to run as soon as possible.
  virtual void PostTask(Closure task) = 0;

  // Posts a task to run as soon as possible after the specified |target_time|.
  virtual void PostTaskForTime(Closure task, TimePoint target_time) = 0;

  // Posts a task to run as soon as possible after the specified |delay|.
  virtual void PostDelayedTask(Closure task, TimeDelta delay) = 0;

  // Returns true if the task runner runs tasks on the current thread.
  virtual bool RunsTasksOnCurrentThread() = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(TaskRunner);

  virtual ~TaskRunner();
};

}  // namespace fxl

#endif  // LIB_FXL_TASKS_TASK_RUNNER_H_
