// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_TASKS_SYNCHRONOUS_TASK_H_
#define SRC_DEVELOPER_CMD_TASKS_SYNCHRONOUS_TASK_H_

#include "src/developer/cmd/tasks/task.h"

namespace cmd {

class SynchronousTask : public Task {
 public:
  using Function = int (*)(int argc, const char** argv);

  explicit SynchronousTask(async_dispatcher_t* dispatcher, Function impl);
  ~SynchronousTask() override;

  // |Task| implementation:
  zx_status_t Execute(Command command, CompletionCallback callback) override;

 private:
  Function impl_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_TASKS_SYNCHRONOUS_TASK_H_
