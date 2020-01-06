// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/getenv_task.h"

#include <stdlib.h>
#include <unistd.h>

namespace cmd {

GetenvTask::GetenvTask(async_dispatcher_t* dispatcher) : Task(dispatcher) {}

GetenvTask::~GetenvTask() = default;

zx_status_t GetenvTask::Execute(Command command, Task::CompletionCallback callback) {
  if (command.args().size() == 1) {
    for (char** variable = environ; *variable; variable++) {
      printf("%s\n", *variable);
    }
  } else if (command.args().size() == 2) {
    const char* name = command.args()[1].c_str();
    const char* value = getenv(name);
    if (value) {
      printf("%s=%s\n", name, value);
    } else {
      fprintf(stderr, "getenv: Invalid environment variable.\n");
    }
  } else {
    fprintf(stderr, "getenv: Invalid number of arguments. Expected 1 or 2, got %zu.\n",
            command.args().size() - 1);
  }
  return ZX_ERR_NEXT;
}

void GetenvTask::Complete(Autocomplete* autocomplete) {
  if (autocomplete->tokens().size() == 1) {
    autocomplete->CompleteAsEnvironmentVariable();
  }
}

}  // namespace cmd
