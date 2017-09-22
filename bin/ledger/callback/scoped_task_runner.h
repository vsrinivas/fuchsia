// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_SCOPED_TASK_RUNNER_H_
#define APPS_LEDGER_SRC_CALLBACK_SCOPED_TASK_RUNNER_H_

#include "peridot/bin/ledger/callback/scoped_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace callback {

// An object with the same interface as |fxl::TaskRunner| that wraps an existing
// |fxl::TaskRunner|, but that is neither copyable nor moveable and will never
// run any task after being deleted.
// Because this class also acts as a WeakPtrFactory, it needs to be the last
// member of a class.
class ScopedTaskRunner {
 public:
  explicit ScopedTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~ScopedTaskRunner();

  // Posts a task to run as soon as possible.
  void PostTask(fxl::Closure task);

  // Posts a task to run as soon as possible after the specified |target_time|.
  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time);

  // Posts a task to run as soon as possible after the specified |delay|.
  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay);

  // Returns true if the task runner runs tasks on the current thread.
  bool RunsTasksOnCurrentThread();

  // Scope the given callback to the current task runner. This means that the
  // given callback will be called when the returned callback is called if and
  // only if this task runner has not been deleted.
  template <typename T>
  auto MakeScoped(T lambda) {
    return callback::MakeScoped(weak_factory_.GetWeakPtr(), std::move(lambda));
  }

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::WeakPtrFactory<ScopedTaskRunner> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedTaskRunner);
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_SCOPED_TASK_RUNNER_H_
