// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>

#include <async/auto_wait.h>
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

#include "garnet/bin/ui/scene_manager/sync/fence.h"

namespace scene_manager {

// Provides access to the consumption fence associated with a call to |Present|.
class AcquireFence {
 public:
  // Takes ownership of the fence.
  // |fence| must be a valid handle.
  explicit AcquireFence(zx::event fence);

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

 private:
  async_wait_result_t OnFenceSignalled(zx_status_t status,
                                       const zx_packet_signal* signal);

  void ClearHandler();

  zx::event fence_;

  async::AutoWait waiter_;
  fxl::Closure ready_callback_;
  bool ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(AcquireFence);
};

}  // namespace scene_manager
