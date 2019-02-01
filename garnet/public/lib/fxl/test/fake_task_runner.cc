// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/test/fake_task_runner.h"

namespace fxl {

FakeTaskRunner::FakeTaskRunner() {}

FakeTaskRunner::~FakeTaskRunner() {}

void FakeTaskRunner::PostTask(Closure task) {
  task_queue_.push(std::move(task));
}

void FakeTaskRunner::PostTaskForTime(Closure task, TimePoint target_time) {
  FXL_LOG(ERROR) << "Not implemented in: FakeTaskRunner::PostTaskForTime";
}

void FakeTaskRunner::PostDelayedTask(Closure task, TimeDelta delay) {
  FXL_LOG(ERROR) << "Not implemented in: FakeTaskRunner::PostDelayedTask";
}

bool FakeTaskRunner::RunsTasksOnCurrentThread() { return true; }

void FakeTaskRunner::Run() {
  FXL_DCHECK(!running_);
  running_ = true;

  while (!should_quit_ && !task_queue_.empty()) {
    Closure task = std::move(task_queue_.front());
    task_queue_.pop();
    task();
  }
  should_quit_ = false;
  running_ = false;
}

void FakeTaskRunner::QuitNow() { should_quit_ = true; }

}  // namespace fxl
