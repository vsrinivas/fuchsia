// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/fd_waiter.h"

#include <zircon/errors.h>

namespace fsl {

FDWaiter::FDWaiter() : io_(nullptr), key_(0) {}

FDWaiter::~FDWaiter() {
  if (io_)
    Cancel();

  FXL_DCHECK(!io_);
  FXL_DCHECK(!key_);
}

bool FDWaiter::Wait(Callback callback,
                    int fd,
                    uint32_t events,
                    fxl::TimeDelta timeout) {
  FXL_DCHECK(!io_);
  FXL_DCHECK(!key_);

  io_ = __fdio_fd_to_io(fd);
  if (!io_)
    return false;

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t signals = ZX_SIGNAL_NONE;
  __fdio_wait_begin(io_, events, &handle, &signals);

  if (handle == ZX_HANDLE_INVALID) {
    Cancel();
    return false;
  }

  key_ = MessageLoop::GetCurrent()->AddHandler(this, handle, signals, timeout);

  // Last to prevent re-entrancy from the move constructor of the callback.
  callback_ = std::move(callback);
  return true;
}

void FDWaiter::Cancel() {
  FXL_DCHECK(io_);

  if (key_)
    MessageLoop::GetCurrent()->RemoveHandler(key_);

  __fdio_release(io_);
  io_ = nullptr;
  key_ = 0;

  // Last to prevent re-entrancy from the destructor of the callback.
  callback_ = Callback();
}

void FDWaiter::OnHandleReady(zx_handle_t handle,
                             zx_signals_t pending,
                             uint64_t count) {
  FXL_DCHECK(io_);
  FXL_DCHECK(key_);

  uint32_t events = 0;
  __fdio_wait_end(io_, pending, &events);

  Callback callback = std::move(callback_);
  Cancel();

  // Last to prevent re-entrancy from the callback.
  callback(ZX_OK, events);
}

void FDWaiter::OnHandleError(zx_handle_t handle, zx_status_t error) {
  Callback callback = std::move(callback_);
  Cancel();

  // Last to prevent re-entrancy from the callback.
  callback(error, 0);
}

}  // namespace fsl
