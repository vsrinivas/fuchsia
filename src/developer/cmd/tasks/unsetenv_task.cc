// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/unsetenv_task.h"

#include <stdlib.h>

namespace cmd {

UnsetenvTask::UnsetenvTask(async_dispatcher_t* dispatcher) : Task(dispatcher) {}

UnsetenvTask::~UnsetenvTask() = default;

zx_status_t UnsetenvTask::Execute(Command command, Task::CompletionCallback callback) {
  if (command.args().size() == 2) {
    const std::string& name = command.args()[1];
    if (name.find('=') != std::string::npos) {
      fprintf(stderr, "unsetenv: Environment variable name cannot contain '=': %s\n", name.c_str());
    } else {
      unsetenv(name.c_str());
    }
  } else {
    fprintf(stderr, "unsetenv: Invalid number of arguments. Expected 1, got %zu.\n",
            command.args().size() - 1);
  }
  return ZX_ERR_NEXT;
}

void UnsetenvTask::Complete(Autocomplete* autocomplete) {
  if (autocomplete->tokens().size() == 1) {
    autocomplete->CompleteAsEnvironmentVariable();
  }
}

}  // namespace cmd
