// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>

#include "fdio_unistd.h"
#include "zxio.h"

__EXPORT
zx_status_t fdio_wait_fd(int fd, uint32_t events, uint32_t* out_pending, zx_time_t deadline) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }
  return fdio_wait(io, events, zx::time(deadline), out_pending);
}

__EXPORT
int fdio_handle_fd(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out,
                   bool shared_handle) {
  auto handle = [h, shared_handle]() -> std::variant<zx::handle, zx::unowned_handle> {
    if (shared_handle) {
      return zx::unowned_handle(h);
    }
    return zx::handle(h);
  };
  zx::result io = fdio_waitable_create(handle(), signals_in, signals_out);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}

__EXPORT
zx_status_t fdio_pipe_half(int* out_fd, zx_handle_t* out_handle) {
  zx::socket h0, h1;
  zx_status_t status = zx::socket::create(0, &h0, &h1);
  if (status != ZX_OK) {
    return status;
  }
  zx::result io = fdio_internal::pipe::create(std::move(h0));
  if (io.is_error()) {
    return io.status_value();
  }
  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    *out_fd = fd.value();
    *out_handle = h1.release();
    return ZX_OK;
  }
  return ZX_ERR_NO_RESOURCES;
}

namespace {

template <typename F>
zx_status_t with_zxio(int fd, F fn, zx_handle_t* out_vmo) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }
  return fn(&io->zxio_storage().io, out_vmo);
}

}  // namespace

__EXPORT
zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo) {
  return with_zxio(fd, zxio_vmo_get_copy, out_vmo);
}

__EXPORT
zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo) {
  return with_zxio(fd, zxio_vmo_get_clone, out_vmo);
}

__EXPORT
zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo) {
  return with_zxio(fd, zxio_vmo_get_exact, out_vmo);
}

__EXPORT
zx_status_t fdio_get_vmo_exec(int fd, zx_handle_t* out_vmo) {
  return with_zxio(fd, zxio_vmo_get_exec, out_vmo);
}
