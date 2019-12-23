// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

typedef struct fdio_waitable {
  zxio_t io;

  // arbitrary handle
  zx_handle_t handle;

  // signals that cause ZXIO_SIGNAL_READABLE
  zx_signals_t readable;

  // signals that cause ZXIO_SIGNAL_WRITABLE
  zx_signals_t writable;

  // if true, don't close handle on close() op
  bool shared_handle;
} fdio_waitable_t;

static_assert(sizeof(fdio_waitable_t) <= sizeof(zxio_storage_t),
              "fdio_waitable_t must fit inside zxio_storage_t.");

static zx_status_t fdio_waitable_close(zxio_t* io) {
  fdio_waitable_t* waitable = reinterpret_cast<fdio_waitable_t*>(io);
  if (!waitable->shared_handle) {
    zx_handle_t handle = waitable->handle;
    waitable->handle = ZX_HANDLE_INVALID;
    zx_handle_close(handle);
  }
  return ZX_OK;
}

static void fdio_waitable_wait_begin(zxio_t* io, zxio_signals_t zxio_signals,
                                     zx_handle_t* out_handle, zx_signals_t* out_zx_signals) {
  fdio_waitable_t* waitable = reinterpret_cast<fdio_waitable_t*>(io);
  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= waitable->readable;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= waitable->writable;
  }
  *out_handle = waitable->handle;
  *out_zx_signals = zx_signals;
}

static void fdio_waitable_wait_end(zxio_t* io, zx_signals_t zx_signals,
                                   zxio_signals_t* out_zxio_signals) {
  fdio_waitable_t* waitable = reinterpret_cast<fdio_waitable_t*>(io);
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & waitable->readable) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & waitable->writable) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  *out_zxio_signals = zxio_signals;
}

static constexpr zxio_ops_t fdio_waitable_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = fdio_waitable_close;
  ops.wait_begin = fdio_waitable_wait_begin;
  ops.wait_end = fdio_waitable_wait_end;
  return ops;
}();

static void fdio_waitable_init(zxio_storage_t* storage, zx_handle_t handle, zx_signals_t readable,
                               zx_signals_t writable, bool shared_handle) {
  fdio_waitable_t* waitable = reinterpret_cast<fdio_waitable_t*>(storage);
  zxio_init(&waitable->io, &fdio_waitable_ops);
  waitable->handle = handle;
  waitable->readable = readable;
  waitable->writable = writable;
  waitable->shared_handle = shared_handle;
}

fdio_t* fdio_waitable_create(zx_handle_t handle, zx_signals_t readable, zx_signals_t writable,
                             bool shared_handle) {
  zxio_storage_t* storage = nullptr;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    if (!shared_handle) {
      zx_handle_close(handle);
    }
    return nullptr;
  }
  fdio_waitable_init(storage, handle, readable, writable, shared_handle);
  return io;
}
