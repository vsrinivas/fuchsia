// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_EXECUTOR_H_
#define SRC_DEVELOPER_CMD_EXECUTOR_H_

#include <map>
#include <string>

#include "src/developer/cmd/command.h"
#include "src/developer/cmd/tasks/task.h"

namespace cmd {

class Executor {
 public:
  explicit Executor(async_dispatcher_t* dispatcher);
  ~Executor();

  // Execute the given command.
  //
  // Returns the |zx_status_t| from the executed |Task|. See |Task::Execute| for
  // documentation.
  zx_status_t Execute(Command command, Task::CompletionCallback callback);

 private:
  async_dispatcher_t* dispatcher_;
  std::map<std::string, Task::Factory> builtin_commands_;
  std::unique_ptr<Task> current_task_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_EXECUTOR_H_
