// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/tracing/lib/test_utils/spawn_and_wait.h"

#include <lib/fdio/spawn.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

static void StringArgvToCArgv(const std::vector<std::string>& argv,
                              std::vector<const char*>* c_argv) {
  for (const auto& arg : argv) {
    c_argv->push_back(arg.c_str());
  }
  c_argv->push_back(nullptr);
}

zx_status_t SpawnProgram(const zx::job& job,
                         const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FXL_VLOG(3) << "Running " << fxl::JoinStrings(argv, " ");

  size_t action_count = 0;
  fdio_spawn_action_t spawn_actions[1];
  if (arg_handle != ZX_HANDLE_INVALID) {
    spawn_actions[0].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    spawn_actions[0].h.id = PA_HND(PA_USER0, 0);
    spawn_actions[0].h.handle = arg_handle;
    action_count = 1;
  }

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  auto status =
      fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, c_argv[0], c_argv.data(),
                     nullptr, action_count, &spawn_actions[0],
                     out_process->reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    FXL_VLOG(3) << "Spawning " << c_argv[0] << " failed: " << err_msg << ", "
                << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t WaitAndGetExitCode(const std::string& program_name,
                               const zx::process& process, int* out_exit_code) {
  // Leave it to the test harness to provide a timeout. If it doesn't that's
  // its bug.
  auto status =
      process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    FXL_VLOG(3) << "Failed waiting for program " << program_name
                << " to exit: " << zx_status_get_string(status);
    return status;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_VLOG(3) << "Error getting return code for program " << program_name
                << ": " << zx_status_get_string(status);
    return status;
  }

  if (proc_info.return_code != 0) {
    FXL_VLOG(3) << program_name << " exited with exit code "
                << proc_info.return_code;
  }
  *out_exit_code = proc_info.return_code;
  return ZX_OK;
}
