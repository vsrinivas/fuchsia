// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLIB_FENCE_SET_LISTENER_H_
#define SRC_UI_LIB_ESCHER_FLIB_FENCE_SET_LISTENER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include "lib/fidl/cpp/vector.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/ui/lib/escher/flib/fence.h"

namespace escher {

// Allows notification of when a set of fences has been signalled.
class FenceSetListener {
 public:
  // Takes ownership of the fences.
  // |fence_listeners| must be valid handles.
  explicit FenceSetListener(std::vector<zx::event> fence_listeners);

  // Invokes the callback when all the fences have been signalled. The callback
  // will be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(fit::closure ready_callback);

  // Returns whether all the fences have been signalled.
  bool ready() const { return num_signalled_fences_ == fences_.size(); }

 private:
  void OnFenceSignalled(zx_koid_t import_koid, zx_status_t status, const zx_packet_signal* signal);

  void ClearHandlers();

  std::vector<zx::event> fences_;
  uint32_t num_signalled_fences_ = 0;

  // Each wait corresponds to an |zx::event| with the same
  // index in |fences_|. The size of this array must match that of |fences_|.
  std::vector<std::unique_ptr<async::Wait>> waiters_;
  // The TaskClosure has to be a unique pointer because Tasks are neither copyable nor movable, and
  // we need to transfer it out of the class and onto the stack before executing the closure stored
  // within.
  std::unique_ptr<async::TaskClosure> task_;

  fit::closure ready_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FenceSetListener);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLIB_FENCE_SET_LISTENER_H_
