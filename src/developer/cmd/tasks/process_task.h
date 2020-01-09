// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_TASKS_PROCESS_TASK_H_
#define SRC_DEVELOPER_CMD_TASKS_PROCESS_TASK_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/types.h>

#include "src/developer/cmd/tasks/task.h"

namespace cmd {

class ProcessTask : public Task {
 public:
  explicit ProcessTask(async_dispatcher_t* dispatcher);
  ~ProcessTask() override;

  // |Task| implementation:
  zx_status_t Execute(Command command, CompletionCallback callback) override;

  static std::string SearchPath(const std::string& name);

  static void CompleteCommand(Autocomplete* autocomplete);

 private:
  void OnProcessTerminated(zx_status_t status);

  CompletionCallback callback_;
  zx::job job_;
  zx::process process_;
  async::Wait waiter_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_TASKS_PROCESS_TASK_H_
