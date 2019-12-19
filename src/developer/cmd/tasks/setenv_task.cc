// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/setenv_task.h"

#include <stdlib.h>

namespace cmd {

SetenvTask::SetenvTask(async_dispatcher_t* dispatcher) : Task(dispatcher) {}

SetenvTask::~SetenvTask() = default;

zx_status_t SetenvTask::Execute(Command command, Task::CompletionCallback callback) {
  if (command.args().size() == 3) {
    const std::string& name = command.args()[1];
    const std::string& value = command.args()[2];
    if (name.find('=') != std::string::npos) {
      fprintf(stderr, "setenv: Environment variable name cannot contain '=': %s\n", name.c_str());
    } else {
      setenv(name.c_str(), value.c_str(), 1);
    }
  } else {
    fprintf(stderr, "setenv: Invalid number of arguments. Expected 2, got %zu.\n",
            command.args().size() - 1);
  }
  return ZX_ERR_NEXT;
}

}  // namespace cmd
