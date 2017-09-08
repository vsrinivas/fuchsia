// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/event.h>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

#include "garnet/bin/ui/scene_manager/fence.h"

namespace scene_manager {

// Provides access to the consumption fence associated with a call to |Present|.
class AcquireFence : private mtl::MessageLoopHandler {
 public:
  // Takes ownership of the fence.
  // |fence| must be a valid handle.
  explicit AcquireFence(mx::event fence);

  // Releases the fence, implicitly signalling to the producer that the
  // buffer is available to be recycled.
  ~AcquireFence();

  // Waits for the fence to indicate that the buffer is ready or for the
  // timeout to expire, whichever comes first.
  bool WaitReady(ftl::TimeDelta timeout = ftl::TimeDelta::Max());

  // Invokes the callback when the fence has been signalled. The callback will
  // be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(ftl::Closure ready_callback);

  // Returns whether this fence has been signalled.
  bool ready() const { return ready_; }

 private:
  // |mtl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  void ClearHandler();

  mx::event fence_;

  mtl::MessageLoop::HandlerKey handler_key_ = 0;
  ftl::Closure ready_callback_;
  bool ready_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(AcquireFence);
};

}  // namespace scene_manager
