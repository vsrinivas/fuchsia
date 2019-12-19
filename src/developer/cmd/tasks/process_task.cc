// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/process_task.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>

#include <memory>
#include <vector>

namespace cmd {

ProcessTask::ProcessTask(async_dispatcher_t* dispatcher)
    : Task(dispatcher), waiter_(ZX_HANDLE_INVALID, ZX_PROCESS_TERMINATED) {
  waiter_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) { OnProcessTerminated(status); });
}

ProcessTask::~ProcessTask() {
  if (job_) {
    job_.kill();
  }
}

zx_status_t ProcessTask::Execute(Command command, Task::CompletionCallback callback) {
  callback_ = std::move(callback);

  zx_status_t status = zx::job::create(*zx::job::default_job(), 0, &job_);
  if (status != ZX_OK) {
    return status;
  }

  const char* path = command.args()[0].c_str();

  std::vector<const char*> argv;
  argv.reserve(command.args().size());
  for (const auto& arg : command.args()) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  status = fdio_spawn(job_.get(), FDIO_SPAWN_CLONE_ALL, path, argv.data(),
                      process_.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  waiter_.set_object(process_.get());
  waiter_.Begin(dispatcher());
  return ZX_ERR_ASYNC;
}

void ProcessTask::OnProcessTerminated(zx_status_t status) {
  job_.kill();
  job_.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), nullptr);
  job_.reset();
  process_.reset();
  auto callback = std::move(callback_);
  callback();
}

}  // namespace cmd
