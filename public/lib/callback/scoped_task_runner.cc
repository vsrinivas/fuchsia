// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/scoped_task_runner.h>

#include <utility>

#include <lib/async/cpp/task.h>

namespace callback {

TaskController::TaskController() = default;
TaskController::~TaskController() = default;

void SimpleTaskController::ShutDown() { alive_ = false; }

void SimpleTaskController::RunTask(fit::closure task) {
  if (alive_)
    task();
}

ScopedTaskRunner::ScopedTaskRunner(async_dispatcher_t* dispatcher)
    : ScopedTaskRunner(SimpleTaskController::Type{}, dispatcher) {}

ScopedTaskRunner::~ScopedTaskRunner() { ShutDown(); }

void ScopedTaskRunner::ShutDown() { controller_->ShutDown(); }

void ScopedTaskRunner::PostTask(fit::closure task) {
  async::PostTask(dispatcher_, MakeScoped(std::move(task)));
}

void ScopedTaskRunner::PostTaskForTime(fit::closure task,
                                       zx::time target_time) {
  async::PostTaskForTime(dispatcher_, MakeScoped(std::move(task)), target_time);
}

void ScopedTaskRunner::PostDelayedTask(fit::closure task, zx::duration delay) {
  async::PostDelayedTask(dispatcher_, MakeScoped(std::move(task)), delay);
}

void ScopedTaskRunner::PostPeriodicTask(fit::closure task,
                                        zx::duration interval,
                                        bool invoke_immediately) {
  if (invoke_immediately) {
    // TODO(rosswang): this is actually incorrect as it may execute the first
    // instance of this task on the wrong thread. Add a test and fix in a
    // subsequent CL.
    task();
  }

  // We live with re-wrapping this task every iteration to greatly simplify the
  // implementation. It's possible to wrap once, but as the scoped task would
  // then need to capture itself, we'd have to declare a dedicated functor.
  // Since this area is not performance-critical, err on the side of simplicity
  // for now.
  PostDelayedTask(
      [this, task = std::move(task), interval]() mutable {
        PostPeriodicTask(std::move(task), interval, true);
      },
      interval);
}

}  // namespace callback
