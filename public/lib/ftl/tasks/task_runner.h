// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TASKS_TASK_RUNNER_H_
#define LIB_FTL_TASKS_TASK_RUNNER_H_

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/time/time.h"

namespace ftl {

class TaskRunner : public RefCountedThreadSafe<TaskRunner> {
 public:
  virtual void PostTask(Closure task) = 0;
  virtual void PostDelayedTask(Closure task, Duration delay) = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(TaskRunner);

  virtual ~TaskRunner();
};

}  // namespace ftl

#endif  // LIB_FTL_TASKS_TASK_RUNNER_H_
