// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/scoped_task_runner.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

namespace callback {

ScopedTaskRunner::ScopedTaskRunner(async_t* async)
    : async_(async), weak_factory_(this) {}

ScopedTaskRunner::~ScopedTaskRunner() {}

void ScopedTaskRunner::PostTask(fit::closure task) {
  async::PostTask(async_, MakeScoped(std::move(task)));
}

void ScopedTaskRunner::PostTaskForTime(fit::closure task,
                                       zx::time target_time) {
  async::PostTaskForTime(async_, MakeScoped(std::move(task)), target_time);
}

void ScopedTaskRunner::PostDelayedTask(fit::closure task, zx::duration delay) {
  async::PostDelayedTask(async_, MakeScoped(std::move(task)), delay);
}

}  // namespace callback
