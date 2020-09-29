// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/mman.h>

#include "unistd.h"

// An implementation of a POSIX memfd.
typedef struct fdio_mem {
  zxio_t io;
} fdio_mem_t;

static_assert(sizeof(fdio_mem_t) <= sizeof(zxio_storage_t),
              "fdio_mem_t must fit inside zxio_storage_t");

// TODO(60236): implement mem operations.

static zx_status_t fdio_mem_close(zxio_t* io) { return ZX_OK; }

static zx_status_t fdio_mem_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                  zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t fdio_mem_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void fdio_mem_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                zx_signals_t* out_zx_signals) {
  *out_handle = ZX_HANDLE_INVALID;
  *out_zx_signals = ZX_SIGNAL_NONE;
}

static void fdio_mem_wait_end(zxio_t* io, zx_signals_t zx_signals,
                              zxio_signals_t* out_zxio_signals) {
  *out_zxio_signals = ZX_SIGNAL_NONE;
}

static constexpr zxio_ops_t fdio_mem_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = fdio_mem_close;
  ops.readv = fdio_mem_readv;
  ops.writev = fdio_mem_writev;
  ops.wait_begin = fdio_mem_wait_begin;
  ops.wait_end = fdio_mem_wait_end;
  return ops;
}();

static void fdio_mem_init(zxio_storage_t* storage) {
  fdio_mem_t* mem = reinterpret_cast<fdio_mem_t*>(storage);
  zxio_init(&mem->io, &fdio_mem_ops);
}

static fdio_t* fdio_mem_create() {
  zxio_storage_t* storage = nullptr;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    return nullptr;
  }
  fdio_mem_init(storage);
  return io;
}

// TODO(60236): remove declaration macros when symbol becomes available in libc
// headers.
__BEGIN_CDECLS

__EXPORT
int memfd_create(const char* name, unsigned int flags) {
  fdio_t* io = nullptr;
  if ((io = fdio_mem_create()) == nullptr) {
    return ERROR(ZX_ERR_NO_MEMORY);
  }

  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_release(io);
  }
  return fd;
}

__END_CDECLS
