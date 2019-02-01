// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_FLIB_FENCE_LISTENER_H_
#define LIB_ESCHER_FLIB_FENCE_LISTENER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/event.h>
#include <zircon/syscalls/port.h>

#include "lib/escher/flib/fence.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace escher {

// Provides access to the consumption fence.
class FenceListener {
 public:
  // Takes ownership of the fence.
  // |fence| must be a valid handle.
  explicit FenceListener(zx::event fence);

  // Waits for the fence to indicate that the buffer is ready or for the
  // timeout to expire, whichever comes first.
  bool WaitReady(fxl::TimeDelta timeout = fxl::TimeDelta::Max());

  // Invokes the callback when the fence has been signalled. The callback will
  // be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(fxl::Closure ready_callback);

  // Returns whether this fence has been signalled.
  bool ready() const { return ready_; }

  const zx::event& event() { return fence_; }

 private:
  void OnFenceSignalled(zx_status_t status, const zx_packet_signal* signal);

  void ClearHandler();

  zx::event fence_;

  async::Wait waiter_;
  fxl::Closure ready_callback_;
  bool ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(FenceListener);
};

}  // namespace escher

#endif  // LIB_ESCHER_FLIB_FENCE_LISTENER_H_
