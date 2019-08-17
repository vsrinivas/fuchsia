// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "launcher.h"

int main(int argc, const char* const* argv) {
  const char* name = "launcher-child";
  zx_handle_t proc = ZX_HANDLE_INVALID;
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  // Note: in order to test that the job policy is properly applied, |zx_process_create| must be
  // called from within the launcher process.
  zx_status_t status = zx_process_create(zx_job_default(), name, strlen(name), 0, &proc, &vmar);
  if (status == ZX_OK) {
    zx_task_kill(proc);
  }
  zx_handle_close(vmar);
  zx_handle_close(proc);
  return status == ZX_OK ? LAUNCHER_SUCCESS : LAUNCHER_FAILURE;
}
