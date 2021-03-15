// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/unsafe.h>

#include "fdio_unistd.h"

__EXPORT
fdio_t* fdio_unsafe_fd_to_io(int fd) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return nullptr;
  }
  return fbl::ExportToRawPtr(&io);
}

__EXPORT
void fdio_unsafe_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle_out,
                            zx_signals_t* signals_out) {
  return io->wait_begin(events, handle_out, signals_out);
}

__EXPORT
void fdio_unsafe_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events_out) {
  return io->wait_end(signals, events_out);
}

__EXPORT
void fdio_unsafe_release(fdio_t* io) { __UNUSED auto release = fbl::ImportFromRawPtr(io); }

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
  if (io == nullptr) {
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  if (io->borrow_channel(&handle) != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return handle;
}
