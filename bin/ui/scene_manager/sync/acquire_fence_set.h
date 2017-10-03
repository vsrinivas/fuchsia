// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>

#include <async/auto_wait.h>
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

#include "garnet/bin/ui/scene_manager/sync/fence.h"

namespace scene_manager {

// Provides access to the consumption fences associated with a call to
// |Present|.
class AcquireFenceSet {
 public:
  // Takes ownership of the fences.
  // |acquire_fences| must be valid handles.
  explicit AcquireFenceSet(::fidl::Array<zx::event> acquire_fences);

  // Invokes the callback when all the fences have been signalled. The callback
  // will be invoked on the current message loop.
  // Can only be called after any previous WaitReadyAsync has invoked the
  // callback. |ready_callback| must be non-null.
  void WaitReadyAsync(fxl::Closure ready_callback);

  // Returns whether all the fences have been signalled.
  bool ready() const { return num_signalled_fences_ == fences_.size(); }

 private:
  async_wait_result_t OnFenceSignalled(zx_koid_t import_koid,
                                       zx_status_t status,
                                       const zx_packet_signal* signal);

  void ClearHandlers();

  ::fidl::Array<zx::event> fences_;
  uint32_t num_signalled_fences_ = 0;

  // async::AutoWait-ers, each corresponding to an |zx::event| with the same
  // index in |fences_|. The size of this array must match that of |fences_|.
  std::vector<std::unique_ptr<async::AutoWait>> waiters_;

  fxl::Closure ready_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AcquireFenceSet);
};

}  // namespace scene_manager
