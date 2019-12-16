// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/scoped_task_runner.h"

#include <lib/async/cpp/task.h>

#include <utility>

namespace ledger {

TaskController::TaskController() = default;
TaskController::~TaskController() = default;

void SimpleTaskController::ShutDown() { alive_ = false; }

void SimpleTaskController::RunTask(fit::closure task) {
  if (alive_)
    task();
}

ScopedTaskRunner::ScopedTaskRunner(async_dispatcher_t* dispatcher)
    : ScopedTaskRunner(SimpleTaskController::Type{}, dispatcher) {}

ScopedTaskRunner::ScopedTaskRunner(ScopedTaskRunner&&) = default;

ScopedTaskRunner::~ScopedTaskRunner() { ShutDown(); }

ScopedTaskRunner& ScopedTaskRunner::operator=(ScopedTaskRunner&&) = default;

void ScopedTaskRunner::ShutDown() { controller_->ShutDown(); }

void ScopedTaskRunner::Reset() { Reset(SimpleTaskController::Type{}); }

void ScopedTaskRunner::PostTask(fit::closure task) {
  async::PostTask(dispatcher_, MakeScoped(std::move(task)));
}

void ScopedTaskRunner::PostTaskForTime(fit::closure task, zx::time target_time) {
  async::PostTaskForTime(dispatcher_, MakeScoped(std::move(task)), target_time);
}

void ScopedTaskRunner::PostDelayedTask(fit::closure task, zx::duration delay) {
  async::PostDelayedTask(dispatcher_, MakeScoped(std::move(task)), delay);
}

void ScopedTaskRunner::PostPeriodicTask(fit::closure task, zx::duration interval, bool invoke_now) {
  // We live with re-wrapping this task every iteration to greatly simplify the
  // implementation. It's possible to wrap once, but as the scoped task would
  // then need to capture itself, we'd have to declare a dedicated functor.
  // Since this area is not performance-critical, err on the side of simplicity
  // for now.
  auto task_iteration = [this, task = std::move(task), interval]() mutable {
    task();
    PostPeriodicTask(std::move(task), interval, false);
  };

  if (invoke_now) {
    PostTask(std::move(task_iteration));
  } else {
    PostDelayedTask(std::move(task_iteration), interval);
  }
}

}  // namespace ledger
