// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/cd_task.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace cmd {

CdTask::CdTask(async_dispatcher_t* dispatcher) : Task(dispatcher) {}

CdTask::~CdTask() = default;

zx_status_t CdTask::Execute(Command command, Task::CompletionCallback callback) {
  if (command.args().size() == 2) {
    if (chdir(command.args()[1].c_str()) < 0) {
      fprintf(stderr, "cd: Failed to change directories: %s\n", strerror(errno));
    }
  } else {
    fprintf(stderr, "cd: Invalid number of arguments. Expected 1, got %zu.\n",
            command.args().size() - 1);
  }
  return ZX_ERR_NEXT;
}

}  // namespace cmd
