// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/scoped_task_runner.h"

#include <utility>

namespace callback {

ScopedTaskRunner::ScopedTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)), weak_factory_(this) {}

ScopedTaskRunner::~ScopedTaskRunner() {}

void ScopedTaskRunner::PostTask(fxl::Closure task) {
  task_runner_->PostTask(MakeScoped(std::move(task)));
}

void ScopedTaskRunner::PostTaskForTime(fxl::Closure task,
                                       fxl::TimePoint target_time) {
  task_runner_->PostTaskForTime(MakeScoped(std::move(task)), target_time);
}

void ScopedTaskRunner::PostDelayedTask(fxl::Closure task,
                                       fxl::TimeDelta delay) {
  task_runner_->PostDelayedTask(MakeScoped(std::move(task)), delay);
}

bool ScopedTaskRunner::RunsTasksOnCurrentThread() {
  return task_runner_->RunsTasksOnCurrentThread();
}

}  // namespace callback
