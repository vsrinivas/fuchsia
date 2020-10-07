// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "unistd.h"

__EXPORT
zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_status_t status = zxio_vmo_get_copy(fdio_get_zxio(io), out_vmo, nullptr);
  fdio_release(io);
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_status_t status = zxio_vmo_get_clone(fdio_get_zxio(io), out_vmo, nullptr);
  fdio_release(io);
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_status_t status = zxio_vmo_get_exact(fdio_get_zxio(io), out_vmo, nullptr);
  fdio_release(io);
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_exec(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_status_t status = zxio_vmo_get_exec(fdio_get_zxio(io), out_vmo, nullptr);
  fdio_release(io);
  return status;
}
