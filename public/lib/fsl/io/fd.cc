// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/fd.h"

#include <fdio/limits.h>
#include <fdio/util.h>
#include <zircon/processargs.h>

namespace fsl {

zx::channel CloneChannelFromFileDescriptor(int fd) {
  zx_handle_t handle[FDIO_MAX_HANDLES];
  uint32_t type[FDIO_MAX_HANDLES];

  zx_status_t r = fdio_clone_fd(fd, 0, handle, type);
  if (r < 0 || r == 0)
    return zx::channel();

  if (type[0] != PA_FDIO_REMOTE) {
    for (int i = 0; i < r; ++i) {
      zx_handle_close(handle[i]);
    }
    return zx::channel();
  }

  // Close any extra handles.
  for (int i = 1; i < r; ++i) {
    zx_handle_close(handle[i]);
  }

  return zx::channel(handle[0]);
}

fxl::UniqueFD OpenChannelAsFileDescriptor(zx::channel channel) {
  zx_handle_t handle = channel.release();
  uint32_t types = PA_FDIO_REMOTE;
  int fd = -1;
  zx_status_t status = fdio_create_fd(&handle, &types, 1, &fd);
  if (status != ZX_OK) {
    return fxl::UniqueFD();
  }
  return fxl::UniqueFD(fd);
}

}  // namespace fsl
