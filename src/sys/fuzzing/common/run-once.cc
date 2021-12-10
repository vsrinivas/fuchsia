// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/run-once.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {

RunOnce::RunOnce(fit::closure task) : task_(std::move(task)) {}

RunOnce::~RunOnce() {
  FX_CHECK(running_);
  ran_.WaitFor("task to complete");
}

void RunOnce::Run() {
  if (running_.exchange(true)) {
    ran_.WaitFor("task to complete");
  } else {
    task_();
    ran_.Signal();
  }
}

}  // namespace fuzzing
