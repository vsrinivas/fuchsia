// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/event.h>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

#include "garnet/bin/ui/scene_manager/fence.h"

namespace scene_manager {

// Provides access to the consumption fences associated with a call to
// |Present|.
class AcquireFenceSet : private fsl::MessageLoopHandler {
 public:
  // Takes ownership of the fences.
  // |acquire_fences| must be valid handles.
  explicit AcquireFenceSet(::fidl::Array<mx::event> acquire_fences);

  // Releases the fence, implicitly signalling to the producer that the
  // buffer is available to be recycled.
  ~AcquireFenceSet();

  // Invokes the callback when all the fences have been signalled. The callback
  // will be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(fxl::Closure ready_callback);

  // Returns whether all the fences have been signalled.
  bool ready() const { return num_signalled_fences_ == fences_.size(); }

 private:
  // |fsl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  void ClearHandlers();

  ::fidl::Array<mx::event> fences_;
  uint32_t num_signalled_fences_ = 0;

  // HandlerKeys, each corresponding to an |mx::event| with the same index in
  // |fences_|. The size of this array must match that of |fences_|.
  std::vector<fsl::MessageLoop::HandlerKey> handler_keys_;

  fxl::Closure ready_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AcquireFenceSet);
};

}  // namespace scene_manager
