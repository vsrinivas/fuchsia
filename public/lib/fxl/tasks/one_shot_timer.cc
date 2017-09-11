// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/tasks/one_shot_timer.h"

#include "lib/fxl/logging.h"

namespace fxl {

OneShotTimer::OneShotTimer() : weak_ptr_factory_(this) {}

OneShotTimer::~OneShotTimer() {
  Stop();
}

void OneShotTimer::Start(TaskRunner* task_runner,
                         const Closure& task,
                         TimeDelta delay) {
  FXL_DCHECK(task_runner);
  FXL_DCHECK(task);

  Stop();
  task_ = task;
  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner->PostDelayedTask(
      [weak_ptr] {
        if (weak_ptr)
          weak_ptr->RunTask();
      },
      delay);
}

void OneShotTimer::Stop() {
  if (task_) {
    task_ = Closure();
    weak_ptr_factory_.InvalidateWeakPtrs();
  }
}

void OneShotTimer::RunTask() {
  if (task_) {
    Closure task;
    task_.swap(task);
    task();
  }
}

}  // namespace fxl
