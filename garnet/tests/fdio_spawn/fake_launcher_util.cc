// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_launcher_util.h"

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <string.h>
#include <zircon/syscalls.h>

// Note: In order to test that the ZX_POL_NEW_PROCESS job policy is properly denied,
// |zx_process_create| must be called from within a process running in the job that the policy was
// applied to.
int main(int argc, const char* const* argv) {
  const char* name = "launcher-child";
  zx::process proc;
  zx::vmar vmar;
  zx_status_t status = proc.create(*zx::job::default_job(), name,
                                   static_cast<uint32_t>(strlen(name)), 0, &proc, &vmar);
  if (status == ZX_OK) {
    proc.kill();
  }
  return status == ZX_OK ? LAUNCHER_SUCCESS : LAUNCHER_FAILURE;
}
