// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <launchpad/vmo.h>

__EXPORT
zx_status_t launchpad_vmo_from_file(const char* filename, zx_handle_t* out) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t vmo;
  zx_handle_t exec_vmo;
  zx_status_t status = fdio_get_vmo_clone(fd, &vmo);
  close(fd);

  if (status != ZX_OK) {
    return status;
  }

  status = zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &exec_vmo);
  if (status != ZX_OK) {
    return status;
  }

  if (strlen(filename) >= ZX_MAX_NAME_LEN) {
    const char* p = strrchr(filename, '/');
    if (p != NULL) {
      filename = p + 1;
    }
  }

  status = zx_object_set_property(exec_vmo, ZX_PROP_NAME, filename, strlen(filename));
  if (status != ZX_OK) {
    zx_handle_close(exec_vmo);
    return status;
  }

  *out = exec_vmo;
  return ZX_OK;
}
