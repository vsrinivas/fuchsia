// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_EXECUTOR_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_EXECUTOR_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <map>
#include <string>
#include <vector>

#include "src/developer/shell/console/command.h"
#include "src/developer/shell/console/err.h"

namespace shell::console {

class Executor {
 public:
  // Provide a |client| where the other endpoint is an interpreter.  Caller retains ownership.
  Executor(llcpp::fuchsia::shell::Shell::SyncClient* client);
  ~Executor();

  // Execute the given command.
  Err Execute(std::unique_ptr<Command> command, fit::closure callback);

  // Terminate the task the executor is currently executing in the foreground, if any.
  void KillForegroundTask();

 private:
  uint64_t context_id_;
  llcpp::fuchsia::shell::Shell::SyncClient* client_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_EXECUTOR_H_
