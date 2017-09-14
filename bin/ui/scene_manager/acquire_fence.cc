// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/acquire_fence.h"

#include <zx/time.h>
#include "lib/fxl/logging.h"

namespace scene_manager {

AcquireFence::AcquireFence(zx::event fence) : fence_(std::move(fence)) {
  FXL_DCHECK(fence_);
}

AcquireFence::~AcquireFence() {
  ClearHandler();
}

bool AcquireFence::WaitReady(fxl::TimeDelta timeout) {
  zx_time_t zx_deadline;
  if (timeout <= fxl::TimeDelta::Zero())
    zx_deadline = 0u;
  else if (timeout == fxl::TimeDelta::Max())
    zx_deadline = ZX_TIME_INFINITE;
  else
    zx_deadline = zx::deadline_after(timeout.ToNanoseconds());

  zx_signals_t pending = 0u;
  while (!ready_) {
    zx_status_t status =
        fence_.wait_one(kFenceSignalledOrClosed, zx_deadline, &pending);
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
    ready_ = pending & kFenceSignalledOrClosed;
    if (zx_deadline != ZX_TIME_INFINITE)
      break;
  }
  return ready_;
}

void AcquireFence::WaitReadyAsync(fxl::Closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FXL_DCHECK(!ready_callback_);

  if (ready_) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(ready_callback));
    return;
  }

  // Returned key will always be non-zero.
  FXL_DCHECK(handler_key_ == 0);
  handler_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
      this, fence_.get(), kFenceSignalledOrClosed);
  ready_callback_ = std::move(ready_callback);
}

void AcquireFence::ClearHandler() {
  if (handler_key_) {
    fsl::MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
    handler_key_ = 0u;
  }
}

void AcquireFence::OnHandleReady(zx_handle_t handle,
                                 zx_signals_t pending,
                                 uint64_t count) {
  FXL_DCHECK(handle == fence_.get());
  FXL_DCHECK(pending & kFenceSignalledOrClosed);
  FXL_DCHECK(ready_callback_);

  // TODO: Handle the case where there is an error condition, probably want to
  // close the session.

  ready_ = true;
  fxl::Closure callback = std::move(ready_callback_);
  ClearHandler();

  callback();
}

}  // namespace scene_manager
