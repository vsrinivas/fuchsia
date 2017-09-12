// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/acquire_fence_set.h"

#include <mx/time.h>
#include "lib/fxl/logging.h"

namespace scene_manager {

AcquireFenceSet::AcquireFenceSet(::fidl::Array<mx::event> acquire_fences)
    : fences_(std::move(acquire_fences)) {}

AcquireFenceSet::~AcquireFenceSet() {
  ClearHandlers();
}

void AcquireFenceSet::WaitReadyAsync(fxl::Closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FXL_DCHECK(!ready_callback_);

  if (ready()) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(ready_callback));
    return;
  }

  FXL_DCHECK(handler_keys_.empty());
  for (auto& fence : fences_) {
    handler_keys_.push_back(fsl::MessageLoop::GetCurrent()->AddHandler(
        this, fence.get(), kFenceSignalledOrClosed));
  }

  ready_callback_ = std::move(ready_callback);
}

void AcquireFenceSet::ClearHandlers() {
  for (auto& handler_key : handler_keys_) {
    // It's possible that a handler got removed earlier (during an
    // OnHandleReady) so check that it's not zero.
    if (handler_key != 0) {
      fsl::MessageLoop::GetCurrent()->RemoveHandler(handler_key);
    }
  }
  handler_keys_.clear();
}

void AcquireFenceSet::OnHandleReady(mx_handle_t handle,
                                    mx_signals_t pending,
                                    uint64_t count) {
  FXL_DCHECK(pending & kFenceSignalledOrClosed);
  FXL_DCHECK(ready_callback_);

  // TODO: Handle the case where there is an error condition, probably want to
  // close the session.
  num_signalled_fences_++;

  // Remove the handler that is associated with this handle.
  FXL_DCHECK(fences_.size() == handler_keys_.size());
  size_t handler_index = 0;
  for (; handler_index < fences_.size(); handler_index++) {
    if (fences_[handler_index].get() == handle) {
      break;
    }
  }
  fsl::MessageLoop::HandlerKey handler_key = handler_keys_[handler_index];
  FXL_DCHECK(handler_key != 0);
  fsl::MessageLoop::GetCurrent()->RemoveHandler(handler_key);
  // Set the handler key to 0 so we don't try to remove it twice.
  handler_keys_[handler_index] = 0;
  if (ready()) {
    fxl::Closure callback = std::move(ready_callback_);
    handler_keys_.clear();

    callback();
  }
}

}  // namespace scene_manager
