// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "private.h"
#include "unistd.h"

namespace fuchsia = ::llcpp::fuchsia;

#define MIN_WINDOW (PAGE_SIZE * 4)
#define MAX_WINDOW ((size_t)64 << 20)

static zx_status_t read_at(fdio_t* io, void* buf, size_t len, off_t offset, size_t* out_actual) {
  size_t actual = 0u;
  zx_status_t status = ZX_OK;
  for (;;) {
    status = zxio_read_at(fdio_get_zxio(io), offset, buf, len, 0, &actual);
    if (status != ZX_ERR_SHOULD_WAIT) {
      break;
    }
    status = fdio_wait(io, FDIO_EVT_READABLE, zx::time::infinite(), NULL);
    if (status != ZX_OK) {
      break;
    }
  }
  if (status != ZX_OK) {
    return status;
  }
  if (actual == 0) {  // EOF (?)
    return ZX_ERR_OUT_OF_RANGE;
  }
  *out_actual = actual;
  return ZX_OK;
}

static zx_status_t read_file_into_vmo(fdio_t* io, zx::vmo* out_vmo) {
  zx_handle_t current_vmar_handle = zx_vmar_root_self();

  fuchsia::io::NodeAttributes attr;
  zx_status_t status = fdio_get_ops(io)->get_attr(io, &attr);
  if (status != ZX_OK) {
    return ZX_ERR_BAD_HANDLE;
  }

  uint64_t size = attr.content_size;
  uint64_t offset = 0;

  zx::vmo vmo;
  status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  while (size > 0) {
    if (size < MIN_WINDOW) {
      // There is little enough left that copying is less overhead
      // than fiddling with the page tables.
      char buffer[PAGE_SIZE];
      size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
      size_t nread;
      status = read_at(io, buffer, xfer, offset, &nread);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(buffer, offset, nread);
      if (status < 0) {
        return status;
      }
      offset += nread;
      size -= nread;
    } else {
      // Map the VMO into our own address space so we can read into
      // it directly and avoid double-buffering.
      size_t chunk = size < MAX_WINDOW ? size : MAX_WINDOW;
      size_t window = (chunk + PAGE_SIZE - 1) & -PAGE_SIZE;
      uintptr_t start = 0;
      status = zx_vmar_map(current_vmar_handle, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo.get(),
                           offset, window, &start);
      if (status != ZX_OK) {
        return status;
      }
      uint8_t* buffer = reinterpret_cast<uint8_t*>(start);
      while (chunk > 0) {
        size_t nread;
        status = read_at(io, buffer, chunk, offset, &nread);
        if (status != ZX_OK) {
          zx_vmar_unmap(current_vmar_handle, start, window);
          return status;
        }
        buffer += nread;
        offset += nread;
        size -= nread;
        chunk -= nread;
      }
      zx_vmar_unmap(current_vmar_handle, start, window);
    }
  }

  *out_vmo = std::move(vmo);
  return ZX_OK;
}

static zx_status_t get_file_vmo(fdio_t* io, zx::vmo* out_vmo) {
  return fdio_get_ops(io)->get_vmo(io, fuchsia::io::VMO_FLAG_READ | fuchsia::io::VMO_FLAG_PRIVATE,
                                   out_vmo);
}

static zx_status_t copy_file_vmo(fdio_t* io, zx::vmo* out_vmo) {
  zx_status_t status = get_file_vmo(io, out_vmo);
  if (status == ZX_OK) {
    return ZX_OK;
  }

  zx::vmo vmo;
  if ((status = read_file_into_vmo(io, &vmo)) == ZX_OK) {
    status =
        vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY | ZX_RIGHT_READ | ZX_RIGHT_MAP, out_vmo);
  }
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx::vmo vmo;
  zx_status_t status = copy_file_vmo(io, &vmo);
  fdio_release(io);
  *out_vmo = vmo.release();
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx::vmo vmo;
  zx_status_t status = get_file_vmo(io, &vmo);
  fdio_release(io);
  *out_vmo = vmo.release();
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx::vmo vmo;
  zx_status_t status =
      fdio_get_ops(io)->get_vmo(io, fuchsia::io::VMO_FLAG_READ | fuchsia::io::VMO_FLAG_EXACT, &vmo);
  fdio_release(io);
  *out_vmo = vmo.release();
  return status;
}

__EXPORT
zx_status_t fdio_get_vmo_exec(int fd, zx_handle_t* out_vmo) {
  fdio_t* io = fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_BAD_HANDLE;
  }

  int flags =
      fuchsia::io::VMO_FLAG_READ | fuchsia::io::VMO_FLAG_EXEC | fuchsia::io::VMO_FLAG_PRIVATE;
  zx::vmo vmo;
  zx_status_t status = fdio_get_ops(io)->get_vmo(io, flags, &vmo);
  fdio_release(io);
  *out_vmo = vmo.release();
  return status;
}
