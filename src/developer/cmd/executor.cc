// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/executor.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>

#include <map>
#include <memory>
#include <vector>

#include "src/developer/cmd/tasks/cd_task.h"
#include "src/developer/cmd/tasks/getenv_task.h"
#include "src/developer/cmd/tasks/process_task.h"
#include "src/developer/cmd/tasks/quit_task.h"
#include "src/developer/cmd/tasks/setenv_task.h"
#include "src/developer/cmd/tasks/unsetenv_task.h"

namespace cmd {
namespace {

template <typename TaskType>
std::unique_ptr<Task> CreateTask(async_dispatcher_t* dispatcher) {
  return std::make_unique<TaskType>(dispatcher);
}

}  // namespace

Executor::Executor(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher),
      builtin_commands_({
          {"cd", &CreateTask<CdTask>},
          {"exit", &CreateTask<QuitTask>},
          {"getenv", &CreateTask<GetenvTask>},
          {"quit", &CreateTask<QuitTask>},
          {"setenv", &CreateTask<SetenvTask>},
          {"unsetenv", &CreateTask<UnsetenvTask>},
      }) {}

Executor::~Executor() = default;

zx_status_t Executor::Execute(Command command, Task::CompletionCallback callback) {
  if (command.is_empty()) {
    return ZX_ERR_NEXT;
  }

  auto it = builtin_commands_.find(command.args()[0]);
  if (it != builtin_commands_.end()) {
    current_task_ = it->second(dispatcher_);
  } else {
    current_task_ = std::make_unique<ProcessTask>(dispatcher_);
  }

  return current_task_->Execute(std::move(command), std::move(callback));
}

}  // namespace cmd
