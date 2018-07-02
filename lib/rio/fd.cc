// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/rio/fd.h"

#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <zircon/processargs.h>

namespace rio {

zx::channel CloneChannel(int fd) {
  zx_handle_t handle[FDIO_MAX_HANDLES];
  uint32_t type[FDIO_MAX_HANDLES];

  zx_status_t r = fdio_clone_fd(fd, 0, handle, type);
  if (r < 0 || r == 0) {
    return zx::channel();
  }

  if (type[0] != PA_FDIO_REMOTE) {
    zx_handle_close_many(handle, r);
    return zx::channel();
  }

  // Close any extra handles.
  if (r > 1) {
    zx_handle_close_many(&handle[1], r - 1);
  }

  return zx::channel(handle[0]);
}

}  // namespace rio
