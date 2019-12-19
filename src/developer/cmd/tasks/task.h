// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_TASKS_TASK_H_
#define SRC_DEVELOPER_CMD_TASKS_TASK_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/cmd/command.h"

namespace cmd {

class Task {
 public:
  using Factory = std::unique_ptr<Task> (*)(async_dispatcher_t* dispatcher);
  using CompletionCallback = fit::closure;

  explicit Task(async_dispatcher_t* dispatcher);
  virtual ~Task();

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&&) = delete;
  Task& operator=(Task&&) = delete;

  // Execute the given command.
  //
  // Can be called at most once for a given |Task| instance.
  //
  // Must return a non-ZX_OK value.
  //
  // If this function returns ZX_ERR_NEXT, then the task is complete and the
  // caller can begin exectuing the next command. In this case, this function
  // must not call |callback|.
  //
  // If this function returns ZX_ERR_ASYNC, then the task is not complete and
  // will complete asynchronously at some point in the future. When the task
  // completes, the task must call |callback|.
  //
  // If this function returns ZX_ERR_STOP, then the task is complete and the
  // caller is not expected to execute further commands.
  //
  // Can also return any other negative zx_status_t value to signal synchronous
  // error. In those cases, this function must not call |callback|.
  virtual zx_status_t Execute(Command command, CompletionCallback callback) = 0;

  // The dispatcher on which the task will schedule asynchronous work, if any.
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

 private:
  async_dispatcher_t* dispatcher_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_TASKS_TASK_H_
