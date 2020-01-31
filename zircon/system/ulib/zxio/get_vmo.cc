// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <limits.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <algorithm>

namespace fio = ::llcpp::fuchsia::io;

static zx_status_t read_at(zxio_t* io, void* buf, size_t len, off_t offset, size_t* out_actual) {
  size_t actual = 0u;
  zx_status_t status;
  for (;;) {
    status = zxio_read_at(io, offset, buf, len, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      zxio_signals_t observed = ZXIO_SIGNAL_NONE;
      status = zxio_wait_one(io, ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_READ_DISABLED, ZX_TIME_INFINITE,
                            &observed);
      if (status != ZX_OK) {
        return status;
      }
      // Retry reading after waiting.
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }
    // Finished |zxio_read_at| successfully.
    break;
  }
  if (actual == 0) {  // EOF (?)
    return ZX_ERR_OUT_OF_RANGE;
  }
  *out_actual = actual;
  return ZX_OK;
}

constexpr size_t kMinWindow = PAGE_SIZE * 4;
constexpr size_t kMaxWindow = 64 << 20;

static zx_status_t read_file_into_vmo(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  auto current_vmar = zx::vmar::root_self();

  zxio_node_attr_t attr;
  zx_status_t status = zxio_attr_get(io, &attr);
  if (status != ZX_OK) {
    return status;
  }

  uint64_t size = attr.content_size;
  uint64_t offset = 0;

  zx::vmo vmo;
  status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  while (size > 0) {
    if (size < kMinWindow) {
      // There is little enough left that copying is less overhead
      // than fiddling with the page tables.
      char buffer[PAGE_SIZE];
      size_t xfer = std::min(size, sizeof(buffer));
      size_t nread;
      status = read_at(io, buffer, xfer, offset, &nread);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(buffer, offset, nread);
      if (status != ZX_OK) {
        return status;
      }
      offset += nread;
      size -= nread;
    } else {
      // Map the VMO into our own address space so we can read into
      // it directly and avoid double-buffering.
      size_t chunk = std::min(size, kMaxWindow);
      size_t window = (chunk + PAGE_SIZE - 1) & -PAGE_SIZE;
      uintptr_t start = 0;
      status =
          current_vmar->map(0, vmo, offset, window, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &start);
      if (status != ZX_OK) {
        return status;
      }
      uint8_t* buffer = reinterpret_cast<uint8_t*>(start);
      while (chunk > 0) {
        size_t nread;
        status = read_at(io, buffer, chunk, offset, &nread);
        if (status != ZX_OK) {
          current_vmar->unmap(start, window);
          return status;
        }
        buffer += nread;
        offset += nread;
        size -= nread;
        chunk -= nread;
      }
      current_vmar->unmap(start, window);
    }
  }

  *out_vmo = vmo.release();
  if (out_size) {
    *out_size = attr.content_size;
  }
  return ZX_OK;
}

zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  zx_status_t status = zxio_vmo_get_clone(io, out_vmo, out_size);
  if (status == ZX_OK) {
    return ZX_OK;
  }
  zx::vmo vmo;
  status = read_file_into_vmo(io, vmo.reset_and_get_address(), out_size);
  if (status != ZX_OK) {
    return status;
  }
  status = vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY | ZX_RIGHT_READ | ZX_RIGHT_MAP, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  *out_vmo = vmo.release();
  return ZX_OK;
}

zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return zxio_vmo_get(io, fio::VMO_FLAG_READ | fio::VMO_FLAG_PRIVATE, out_vmo, out_size);
}

zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return zxio_vmo_get(io, fio::VMO_FLAG_READ | fio::VMO_FLAG_EXACT, out_vmo, out_size);
}

zx_status_t zxio_vmo_get_exec(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return zxio_vmo_get(io, fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC | fio::VMO_FLAG_PRIVATE, out_vmo,
                      out_size);
}
