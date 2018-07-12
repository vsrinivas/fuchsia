// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/logging.h"

#include <lib/async/default.h>
#include <zircon/errors.h>

namespace fsl {

FDWaiter::FDWaiter(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), io_(nullptr) {
  FXL_DCHECK(dispatcher_);
}

FDWaiter::~FDWaiter() {
  if (io_) {
    Cancel();
  }

  FXL_DCHECK(!io_);
}

bool FDWaiter::Wait(Callback callback, int fd, uint32_t events) {
  FXL_DCHECK(!io_);

  io_ = __fdio_fd_to_io(fd);
  if (!io_) {
    return false;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t signals = ZX_SIGNAL_NONE;
  __fdio_wait_begin(io_, events, &handle, &signals);

  if (handle == ZX_HANDLE_INVALID) {
    Release();
    return false;
  }

  wait_.set_object(handle);
  wait_.set_trigger(signals);
  zx_status_t status = wait_.Begin(dispatcher_);

  if (status != ZX_OK) {
    Release();
    return false;
  }

  // Last to prevent re-entrancy from the move constructor of the callback.
  callback_ = std::move(callback);
  return true;
}

void FDWaiter::Release() {
  FXL_DCHECK(io_);
  __fdio_release(io_);
  io_ = nullptr;
}

void FDWaiter::Cancel() {
  if (io_) {
    wait_.Cancel();
    Release();

    // Last to prevent re-entrancy from the destructor of the callback.
    callback_ = Callback();
  }
}

void FDWaiter::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                       zx_status_t status, const zx_packet_signal_t* signal) {
  FXL_DCHECK(io_);

  uint32_t events = 0;
  if (status == ZX_OK) {
    __fdio_wait_end(io_, signal->observed, &events);
  }

  Callback callback = std::move(callback_);
  Release();

  // Last to prevent re-entrancy from the callback.
  callback(status, events);
}

}  // namespace fsl
