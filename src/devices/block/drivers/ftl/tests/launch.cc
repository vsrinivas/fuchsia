// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

zx::process Launch(const char* const* argv) {
  ZX_ASSERT(argv);

  fdio_spawn_action_t actions[1] = {{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "worker"},
  }};
  char error[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t subprocess;
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, NULL,
                                      1, actions, &subprocess, error);
  if (status != ZX_OK) {
    printf("Subprocess launch failed: %s\n", error);
    subprocess = ZX_HANDLE_INVALID;
  }
  return zx::process(subprocess);
}

bool WaitForExit(const zx::process& process, int64_t* exit_code) {
  zx_signals_t signals_observed = 0;
  zx_status_t status =
      process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &signals_observed);

  if (status != ZX_OK) {
    printf("zx_object_wait_one failed, status: %d\n", status);
    return false;
  }

  zx_info_process_v2_t proc_info;
  status = process.get_info(ZX_INFO_PROCESS_V2, &proc_info, sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    printf("zx_object_get_info failed, status: %d\n", status);
    return false;
  }

  *exit_code = proc_info.return_code;
  return true;
}

}  // namespace.

int Execute(const char** argv) {
  zx::process process = Launch(argv);
  if (process == ZX_HANDLE_INVALID) {
    return -1;
  }

  int64_t exit_code;
  if (!WaitForExit(process, &exit_code)) {
    printf("Unable to get return code\n");
    return -1;
  }
  return static_cast<int>(exit_code);
}
