// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/run-once.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {

RunOnce::RunOnce(fit::closure task) : task_(std::move(task)) {}

RunOnce::~RunOnce() {
  FX_CHECK(running_);
  sync_completion_wait(&ran_, ZX_TIME_INFINITE);
}

void RunOnce::Run() {
  if (running_.exchange(true)) {
    sync_completion_wait(&ran_, ZX_TIME_INFINITE);
  } else {
    task_();
    sync_completion_signal(&ran_);
  }
}

}  // namespace fuzzing
