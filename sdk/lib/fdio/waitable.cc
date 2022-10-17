// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include "sdk/lib/fdio/zxio.h"

struct fdio_waitable_t {
  zxio_t io;

  // arbitrary handle
  std::variant<zx::handle, zx::unowned_handle> handle;

  // signals that cause ZXIO_SIGNAL_READABLE
  zx_signals_t readable;

  // signals that cause ZXIO_SIGNAL_WRITABLE
  zx_signals_t writable;
};

static_assert(sizeof(fdio_waitable_t) <= sizeof(zxio_storage_t),
              "fdio_waitable_t must fit inside zxio_storage_t.");

static zx_status_t fdio_waitable_close(zxio_t* io) {
  auto waitable = reinterpret_cast<fdio_waitable_t*>(io);
  waitable->~fdio_waitable_t();
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
  std::visit(fdio::overloaded{
                 [out_handle](zx::handle& handle) { *out_handle = handle.get(); },
                 [out_handle](zx::unowned_handle& handle) { *out_handle = handle->get(); },
             },
             waitable->handle);
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

zx::result<fdio_ptr> fdio_waitable_create(std::variant<zx::handle, zx::unowned_handle> handle,
                                          zx_signals_t readable, zx_signals_t writable) {
  zx::result io = fdio_internal::zxio::create();
  if (io.is_error()) {
    return io.take_error();
  }
  auto waitable = new (&io->zxio_storage()) fdio_waitable_t{
      .handle = std::move(handle),
      .readable = readable,
      .writable = writable,
  };
  zxio_init(&waitable->io, &fdio_waitable_ops);
  return io;
}
