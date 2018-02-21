// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/launch.h"

#include <fdio/io.h>
#include <launchpad/launchpad.h>

zx_status_t Launch(const std::vector<std::string>& argv, zx::process* process) {
  launchpad_t* lp;
  zx_status_t status = launchpad_create(0, argv[0].c_str(), &lp);
  if (status != ZX_OK)
    return status;

  status = launchpad_load_from_file(lp, argv[0].c_str());
  if (status != ZX_OK)
    return status;

  // Command line arguments.
  if (argv.size() > 1) {
    std::vector<const char*> arg_ptrs(argv.size() - 1);
    for (size_t i = 0; i < argv.size() - 1; i++)
      arg_ptrs[i] = argv[i + 1].c_str();
    status = launchpad_set_args(lp, arg_ptrs.size(), arg_ptrs.data());
    if (status != ZX_OK)
      return status;
  }

  // Transfer STDIO handles.
  status = launchpad_transfer_fd(lp, 1, FDIO_FLAG_USE_FOR_STDIO | 0);
  if (status != ZX_OK)
    return status;

  launchpad_clone(
      lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
  if (status != ZX_OK)
    return status;

  zx_handle_t child;
  status = launchpad_go(lp, &child, nullptr);
  if (status != ZX_OK)
    return status;

  *process = zx::process(child);
  return ZX_OK;
}
