// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/synchronous_task.h"

namespace cmd {

SynchronousTask::SynchronousTask(async_dispatcher_t* dispatcher, Function impl)
    : Task(dispatcher), impl_(impl) {}

SynchronousTask::~SynchronousTask() = default;

zx_status_t SynchronousTask::Execute(Command command, Task::CompletionCallback callback) {
  std::vector<const char*> argv;
  argv.reserve(command.args().size());
  for (const auto& arg : command.args()) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);
  impl_(command.args().size(), argv.data());
  return ZX_ERR_NEXT;
}

}  // namespace cmd
