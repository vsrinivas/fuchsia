// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <launchpad/vmo.h>

namespace fio = ::llcpp::fuchsia::io;

__EXPORT
zx_status_t launchpad_vmo_from_file(const char* filename, zx_handle_t* out) {
  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd(filename, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                                    fd.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo exec_vmo;
  status = fdio_get_vmo_exec(fd.get(), exec_vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  if (strlen(filename) >= ZX_MAX_NAME_LEN) {
    const char* p = strrchr(filename, '/');
    if (p != NULL) {
      filename = p + 1;
    }
  }

  status = exec_vmo.set_property(ZX_PROP_NAME, filename, strlen(filename));
  if (status != ZX_OK) {
    return status;
  }

  *out = exec_vmo.release();
  return ZX_OK;
}
