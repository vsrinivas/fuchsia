// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/launcher.h"

#include <fdio/io.h>
#include <launchpad/launchpad.h>

zx_status_t Launcher::Setup(const std::vector<std::string>& argv) {
  zx_status_t status = launchpad_create(0, argv[0].c_str(), &lp_);
  if (status != ZX_OK)
    return status;

  status = launchpad_load_from_file(lp_, argv[0].c_str());
  if (status != ZX_OK)
    return status;

  // Command line arguments.
  if (argv.size() > 1) {
    std::vector<const char*> arg_ptrs(argv.size() - 1);
    for (size_t i = 0; i < argv.size() - 1; i++)
      arg_ptrs[i] = argv[i + 1].c_str();
    status = launchpad_set_args(lp_, arg_ptrs.size(), arg_ptrs.data());
    if (status != ZX_OK)
      return status;
  }

  /*
  Transfering STDIO handles is currently disabled. When doing local debugging
  sharing stdio currently leaves the debugger UI in an inconsistent state and
  stdout doesn't work. Instead we need to redirect stdio in a way the debugger
  can control.

  // Transfer STDIO handles.
  status = launchpad_transfer_fd(lp_, 1, FDIO_FLAG_USE_FOR_STDIO | 0);
  if (status != ZX_OK)
    return status;
  */
  launchpad_clone(
      lp_, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
  if (status != ZX_OK)
    return status;

  return ZX_OK;
}

zx::process Launcher::GetProcess() const {
  zx_handle_t duplicate_process = ZX_HANDLE_INVALID;
  zx_handle_duplicate(launchpad_get_process_handle(lp_), ZX_RIGHT_SAME_RIGHTS,
                      &duplicate_process);
  return zx::process(duplicate_process);
}

zx_status_t Launcher::Start() {
  zx_handle_t child;
  zx_status_t status = launchpad_go(lp_, &child, nullptr);
  if (status != ZX_OK)
    return status;

  // If the caller needed a handle to the process it would have called
  // GetProcess() to clone the handle before this.
  zx_handle_close(child);
  lp_ = nullptr;
  return ZX_OK;
}
