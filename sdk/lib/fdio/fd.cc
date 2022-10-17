// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>

#include <variant>

#include <fbl/auto_lock.h>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/internal.h"

__EXPORT
zx_status_t fdio_fd_create(zx_handle_t handle, int* fd_out) {
  zx::result io = fdio::create(zx::handle(handle));
  if (io.is_error()) {
    return io.status_value();
  }
  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    *fd_out = fd.value();
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

__EXPORT
zx_status_t fdio_cwd_clone(zx_handle_t* out_handle) {
  fdio_ptr cwd = []() {
    fbl::AutoLock lock(&fdio_lock);
    return fdio_cwd_handle.get();
  }();
  return cwd->clone(out_handle);
}

__EXPORT
zx_status_t fdio_fd_clone(int fd, zx_handle_t* out_handle) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  // TODO(fxbug.dev/30920): implement/honor close-on-exec flag
  return io->clone(out_handle);
}

__EXPORT
zx_status_t fdio_fd_transfer(int fd, zx_handle_t* out_handle) {
  fdio_ptr io = unbind_from_fd(fd);
  if (io == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::variant reference = GetLastReference(std::move(io));
  auto* ptr = std::get_if<fdio::last_reference>(&reference);
  if (ptr) {
    return ptr->unwrap(out_handle);
  }
  return ZX_ERR_UNAVAILABLE;
}

__EXPORT
zx_status_t fdio_fd_transfer_or_clone(int fd, zx_handle_t* out_handle) {
  fdio_ptr io = unbind_from_fd(fd);
  if (io == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return std::visit(fdio::overloaded{[out_handle](fdio::last_reference reference) {
                                       return reference.unwrap(out_handle);
                                     },
                                     [out_handle](fdio_ptr ptr) { return ptr->clone(out_handle); }},
                    GetLastReference(std::move(io)));
}
