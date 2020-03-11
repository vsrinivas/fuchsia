// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLIB_FENCE_LISTENER_H_
#define SRC_UI_LIB_ESCHER_FLIB_FENCE_LISTENER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/port.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/lib/escher/flib/fence.h"

namespace escher {

// Provides access to the consumption fence.
class FenceListener {
 public:
  // Takes ownership of the fence.
  // |fence| must be a valid handle.
  explicit FenceListener(zx::event fence);

  // Waits for the fence to indicate that the buffer is ready or for the
  // timeout to expire, whichever comes first.
  bool WaitReady(zx::duration timeout = zx::duration::infinite());

  // Invokes the callback when the fence has been signalled. The callback will
  // be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(fit::closure ready_callback);

  // Returns whether this fence has been signalled.
  bool ready() const { return ready_; }

  const zx::event& event() { return fence_; }

 private:
  void OnFenceSignalled(zx_status_t status, const zx_packet_signal* signal);

  void ClearHandler();

  zx::event fence_;

  async::Wait waiter_;
  fit::closure ready_callback_;
  bool ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(FenceListener);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLIB_FENCE_LISTENER_H_
