// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/process_task.h"

#include <lib/fdio/spawn.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/process.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <vector>

namespace cmd {
namespace {

const char kDefaultPath[] = ZX_SHELL_ENV_PATH_VALUE;

}  // namespace

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

  std::string executable = SearchPath(command.args()[0]);
  if (executable.empty()) {
    return ZX_ERR_NOT_FOUND;
  }

  std::vector<const char*> argv;
  argv.reserve(command.args().size());
  for (const auto& arg : command.args()) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  status = fdio_spawn(job_.get(), FDIO_SPAWN_CLONE_ALL, executable.c_str(), argv.data(),
                      process_.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  waiter_.set_object(process_.get());
  waiter_.Begin(dispatcher());
  return ZX_ERR_ASYNC;
}

std::string ProcessTask::SearchPath(const std::string& name) {
  if (name.find('/') != std::string::npos) {
    return name;
  }
  const char* path = getenv("PATH");
  if (!path) {
    path = kDefaultPath;
  }

  std::istringstream input;
  input.str(path);
  for (std::string prefix; std::getline(input, prefix, ':');) {
    std::string fullname = prefix + '/' + name;
    struct stat sb = {};
    if (stat(fullname.c_str(), &sb) < 0 || !S_ISREG(sb.st_mode)) {
      continue;
    }
    return fullname;
  }

  return std::string();
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
