// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zxio/ops.h>
#include <zircon/errors.h>

#include <variant>

#include <fbl/auto_lock.h>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/internal.h"
#include "sdk/lib/fdio/zxio.h"

__EXPORT
fdio_t* fdio_default_create(void) {
  zx::result io = fdio_internal::zxio::create();
  if (io.is_error()) {
    return nullptr;
  }
  std::variant reference = GetLastReference(std::move(io.value()));
  return std::get<fdio::last_reference>(reference).ExportToRawPtr();
}

__EXPORT
fdio_t* fdio_null_create(void) {
  zx::result io = fdio_internal::zxio::create_null();
  if (io.is_error()) {
    return nullptr;
  }
  std::variant reference = GetLastReference(std::move(io.value()));
  return std::get<fdio::last_reference>(reference).ExportToRawPtr();
}

__EXPORT
zx_status_t fdio_create(zx_handle_t h, fdio_t** out_io) {
  zx::result io = fdio::create(zx::handle(h));
  if (io.is_ok()) {
    std::variant reference = GetLastReference(std::move(io.value()));
    *out_io = std::get<fdio::last_reference>(reference).ExportToRawPtr();
  }
  return io.status_value();
}

__EXPORT
int fdio_fd_create_null(void) {
  zx::result io = fdio_internal::zxio::create_null();
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
extern "C" zxio_t* fdio_get_zxio(fdio_t* io) { return &io->zxio_storage().io; }

__EXPORT
int fdio_bind_to_fd(fdio_t* io, int fd, int starting_fd) {
  fdio_ptr owned = fbl::ImportFromRawPtr(io);
  // If we are not given an |fd|, the |starting_fd| must be non-negative.
  if ((fd < 0 && starting_fd < 0) || fd >= FDIO_MAX_FD) {
    return ERRNO(EINVAL);
  }

  // Don't release under lock.
  fdio_ptr io_to_close = nullptr;
  {
    fbl::AutoLock lock(&fdio_lock);
    if (fd < 0) {
      // A negative fd implies that any free fd value can be used
      // TODO: bitmap, ffs, etc
      for (fd = starting_fd; fd < FDIO_MAX_FD; fd++) {
        if (fdio_fdtab[fd].try_set(owned)) {
          return fd;
        }
      }
      return ERRNO(EMFILE);
    }
    io_to_close = fdio_fdtab[fd].replace(owned);
  }
  return fd;
}

__EXPORT
zx_status_t fdio_unbind_from_fd(int fd, fdio_t** out) {
  fdio_ptr io = unbind_from_fd(fd);
  if (io == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::variant reference = GetLastReference(std::move(io));
  auto* ptr = std::get_if<fdio::last_reference>(&reference);
  if (ptr) {
    *out = ptr->ExportToRawPtr();
    return ZX_OK;
  }
  return ZX_ERR_UNAVAILABLE;
}

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) { return fdio_fd_transfer(fd, out); }

__EXPORT
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) {
  zx::result io = fdio_internal::zxio::create();
  if (io.is_error()) {
    return nullptr;
  }
  *out_storage = &io->zxio_storage();
  std::variant reference = GetLastReference(std::move(io.value()));
  return std::get<fdio::last_reference>(reference).ExportToRawPtr();
}
