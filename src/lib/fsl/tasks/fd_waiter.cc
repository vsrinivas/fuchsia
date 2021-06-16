// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/tasks/fd_waiter.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace fsl {

FDWaiter::FDWaiter(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), io_(nullptr) {
  FX_DCHECK(dispatcher_);
}

FDWaiter::~FDWaiter() {
  if (io_) {
    Cancel();
  }

  FX_DCHECK(!io_);
}

bool FDWaiter::Wait(Callback callback, int fd, uint32_t events) {
  std::lock_guard<std::mutex> guard(mutex_);
  FX_DCHECK(!io_);

  io_ = fdio_unsafe_fd_to_io(fd);
  if (!io_) {
    return false;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t signals = ZX_SIGNAL_NONE;
  fdio_unsafe_wait_begin(io_, events, &handle, &signals);

  if (handle == ZX_HANDLE_INVALID) {
    ReleaseLocked();
    return false;
  }

  wait_.set_object(handle);
  wait_.set_trigger(signals);
  zx_status_t status = wait_.Begin(dispatcher_);
  if (status != ZX_OK) {
    ReleaseLocked();
    return false;
  }

  callback_ = std::move(callback);
  return true;
}

void FDWaiter::ReleaseLocked() {
  FX_DCHECK(io_);
  fdio_unsafe_release(io_);
  io_ = nullptr;
}

void FDWaiter::Cancel() {
  // Callback's destructor may ultimately call back into this object (e.g. to Cancel()) so take care
  // to avoid destroying any Callbacks while holding the lock.
  Callback to_be_destroyed;
  std::lock_guard<std::mutex> guard(mutex_);
  if (io_) {
    wait_.Cancel();
    ReleaseLocked();
    to_be_destroyed = std::move(callback_);
  }
}

void FDWaiter::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
  Callback callback;
  uint32_t events = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    FX_DCHECK(io_);

    if (status == ZX_OK) {
      fdio_unsafe_wait_end(io_, signal->observed, &events);
    }

    callback = std::move(callback_);
    ReleaseLocked();
  }

  callback(status, events);
}

}  // namespace fsl
