// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/syscalls.h>

#include <launchpad/launchpad.h>

int main(int argc, const char* const* argv) {
  launchpad_t* lp = NULL;
  launchpad_create(ZX_HANDLE_INVALID, "launcher-child", &lp);
  launchpad_load_from_file(lp, argv[1]);
  launchpad_set_args(lp, argc - 1, argv + 1);
  launchpad_clone(lp, LP_CLONE_ALL);

  zx_handle_t process = ZX_HANDLE_INVALID;
  zx_status_t status = launchpad_go(lp, &process, NULL);
  if (status != ZX_OK)
    return 401;

  status = zx_object_wait_one(process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL);
  if (status != ZX_OK)
    return status;

  zx_info_process_t proc_info;
  memset(&proc_info, 0, sizeof(proc_info));
  status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
  if (status != ZX_OK)
    return status;

  return proc_info.return_code;
}
