// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/job.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <iostream>
#include <vector>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

// ProcessSpawner is a simple utility that waits for user input on stdin and
// creates a new process when anything that doens't say "exit" in it is entered.
//
// This is useful for debugging process attaching and similar functionality.

namespace {

uint64_t GetKoidForHandle(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t res =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
  if (res != ZX_OK)
    return 0;
  return static_cast<uint64_t>(info.koid);
}

zx_status_t LaunchProcess(zx_handle_t job, const std::vector<const char*>& argv, const char* name,
                          int outfd, zx_handle_t* proc) {
  std::vector<const char*> normalized_argv = argv;
  normalized_argv.push_back(nullptr);

  fdio_spawn_action_t actions[] = {
      {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = outfd, .target_fd = STDOUT_FILENO}},
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name}}};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status =
      fdio_spawn_etc(job, FDIO_SPAWN_CLONE_ALL, normalized_argv.front(), normalized_argv.data(),
                     nullptr, arraysize(actions), actions, proc, err_msg);
  return status;
}

}  // namespace

const char kBinaryPath[] = "/pkgfs/packages/debug_agent_tests/0/bin/process_loop";

int main() {
  zx_handle_t default_job = zx_job_default();
  zx_handle_t child_job;
  if (zx_status_t status = zx_job_create(default_job, 0u, &child_job); status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not create a child job.";
    exit(1);
  }

  FXL_LOG(INFO) << "Parent job: " << GetKoidForHandle(default_job)
                << ", Created job: " << GetKoidForHandle(child_job);

  // We're going to keep a list of the created processes.
  struct Process {
    std::string name;
    zx_handle_t proc_handle;
  };
  std::vector<Process> processes;

  FXL_LOG(INFO) << "Waiting for output.";
  std::vector<char> current_line;
  while (true) {
    char c = getc(stdin);
    printf("%c", c);
    fflush(stdout);

    // We acumulate characters to that the user can write exit if they want to
    // exit. Not a very good UI, but works for the testing purposes.
    if (c >= 'a' && c <= 'z') {
      current_line.push_back(c);
      continue;
    }
    current_line.push_back(0);

    // If the user wrote exit somewhere, we exit.
    std::string cmd(current_line.data());
    if (cmd.find("exit") != std::string::npos) {
      FXL_LOG(INFO) << "Found \"exit\" in the input. Exiting.";
      exit(0);
    }

    // Spawn a process the fdio way.
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
      FXL_LOG(ERROR) << "Could not create pipes!";
      exit(1);
    }

    FXL_LOG(INFO) << "Creating process.";
    Process process;
    process.name = fxl::StringPrintf("process-%lu", processes.size());
    zx_status_t status = LaunchProcess(child_job, {kBinaryPath}, process.name.data(), pipe_fds[0],
                                       &process.proc_handle);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not create process " << process.name << ": "
                     << zx_status_get_string(status);
      exit(1);
    }

    FXL_LOG(INFO) << "Created process " << process.name
                  << " with KOID: " << GetKoidForHandle(process.proc_handle);
    processes.push_back(std::move(process));
    current_line.clear();
  }

  return 0;
}
