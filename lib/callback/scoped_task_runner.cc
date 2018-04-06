// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/callback/scoped_task_runner.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

namespace callback {

ScopedTaskRunner::ScopedTaskRunner(async_t* async)
    : async_(async), weak_factory_(this) {}

ScopedTaskRunner::ScopedTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner)
    :  async_(nullptr), task_runner_(std::move(task_runner)), weak_factory_(this) {}

ScopedTaskRunner::~ScopedTaskRunner() {}

void ScopedTaskRunner::PostTask(fxl::Closure task) {
  if (async_){
    async::PostTask(async_, MakeScoped(std::move(task)));
  } else {
    task_runner_->PostTask(MakeScoped(std::move(task)));
  }
}

void ScopedTaskRunner::PostTaskForTime(fxl::Closure task,
                                       fxl::TimePoint target_time) {
  if (async_){
    async::PostTaskForTime(async_, MakeScoped(std::move(task)),
                           zx::time(target_time.ToEpochDelta().ToNanoseconds()));
  } else {
    task_runner_->PostTaskForTime(MakeScoped(std::move(task)), target_time);
  }
}

void ScopedTaskRunner::PostDelayedTask(fxl::Closure task,
                                       fxl::TimeDelta delay) {
  if (async_){
    async::PostDelayedTask(async_, MakeScoped(std::move(task)),
                           zx::nsec(delay.ToNanoseconds()));
  } else {
    task_runner_->PostDelayedTask(MakeScoped(std::move(task)),delay);
  }
}
bool ScopedTaskRunner::RunsTasksOnCurrentThread() {
  if (async_) {
    return async_ == async_get_default();
  } else {
    return task_runner_->RunsTasksOnCurrentThread();
  }
}

}  // namespace callback
