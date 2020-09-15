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

#include "src/developer/shell/common/err.h"
#include "src/developer/shell/console/command.h"

namespace shell::console {

class Executor {
 public:
  // Provide a |client| where the other endpoint is an interpreter.  Caller retains ownership.
  Executor(llcpp::fuchsia::shell::Shell::SyncClient* client);
  ~Executor();

  // Execute the given command.
  // The standard output should be passed to |out_callback|.
  // The error output should be passed to |err_callback|.
  // |done_callback| will be called exactly once, when we are done computing.
  Err Execute(std::unique_ptr<Command> command,
              fit::function<void(const std::string&)> out_callback,
              fit::function<void(const std::string&)> err_callback,
              fit::callback<void()> done_callback);

  // Terminate the task the executor is currently executing in the foreground, if any.
  void KillForegroundTask();

 private:
  uint64_t context_id_;
  llcpp::fuchsia::shell::Shell::SyncClient* client_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_EXECUTOR_H_
