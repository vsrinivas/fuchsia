// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/quit_task.h"

namespace cmd {

QuitTask::QuitTask(async_dispatcher_t* dispatcher) : Task(dispatcher) {}

QuitTask::~QuitTask() = default;

zx_status_t QuitTask::Execute(Command command, Task::CompletionCallback callback) {
  return ZX_ERR_STOP;
}

}  // namespace cmd
