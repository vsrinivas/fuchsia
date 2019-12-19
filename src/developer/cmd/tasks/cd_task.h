// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_TASKS_CD_TASK_H_
#define SRC_DEVELOPER_CMD_TASKS_CD_TASK_H_

#include "src/developer/cmd/tasks/task.h"

namespace cmd {

class CdTask : public Task {
 public:
  explicit CdTask(async_dispatcher_t* dispatcher);
  ~CdTask() override;

  // |Task| implementation:
  zx_status_t Execute(Command command, CompletionCallback callback) override;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_TASKS_CD_TASK_H_
