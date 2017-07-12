// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_FENCE_H_
#define APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_FENCE_H_

#include <mx/eventpair.h>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace mozart {

// Provides access to the consumption fence associated with a buffer.
class BufferFence : private mtl::MessageLoopHandler {
 public:
  // Takes ownership of the fence.
  // |fence| must be a valid handle.
  explicit BufferFence(mx::eventpair fence);

  // Releases the fence, implicitly signalling to the producer that the
  // buffer is available to be recycled.
  ~BufferFence();

  // Waits for the fence to indicate that the buffer is ready or for the
  // timeout to expire, whichever comes first.
  bool WaitReady(ftl::TimeDelta timeout = ftl::TimeDelta::Max());

  // Invokes the callback when the buffer becomes ready to consume as indicated
  // by the fence's signal state.  The callback will be invoked on the
  // current message loop.
  void SetReadyCallback(ftl::Closure ready_callback);

 private:
  // |mtl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  void ClearReadyCallback();

  mx::eventpair fence_;

  mtl::MessageLoop::HandlerKey handler_key_{};
  ftl::Closure ready_callback_;
  bool ready_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(BufferFence);
};

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_FENCE_H_
