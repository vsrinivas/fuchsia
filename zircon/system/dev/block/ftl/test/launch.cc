// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

zx::process Launch(int argc, const char** argv) {
  ZX_ASSERT(argc);
  ZX_ASSERT(argv);

  launchpad_t* launchpad;
  launchpad_create(0, "worker", &launchpad);
  launchpad_load_from_file(launchpad, argv[0]);
  launchpad_set_args(launchpad, argc, argv);
  launchpad_clone(launchpad, LP_CLONE_ALL);

  const char* error;
  zx_handle_t subprocess;
  if (launchpad_go(launchpad, &subprocess, &error) != ZX_OK) {
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

  zx_info_process_t proc_info;
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    printf("zx_object_get_info failed, status: %d\n", status);
    return false;
  }

  *exit_code = proc_info.return_code;
  return true;
}

}  // namespace.

int Execute(int argc, const char** argv) {
  zx::process process = Launch(argc, argv);
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
