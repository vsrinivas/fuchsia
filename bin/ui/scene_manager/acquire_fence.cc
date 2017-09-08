// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/acquire_fence.h"

#include <mx/time.h>
#include "lib/ftl/logging.h"

namespace scene_manager {

AcquireFence::AcquireFence(mx::event fence) : fence_(std::move(fence)) {
  FTL_DCHECK(fence_);
}

AcquireFence::~AcquireFence() {
  ClearHandler();
}

bool AcquireFence::WaitReady(ftl::TimeDelta timeout) {
  mx_time_t mx_deadline;
  if (timeout <= ftl::TimeDelta::Zero())
    mx_deadline = 0u;
  else if (timeout == ftl::TimeDelta::Max())
    mx_deadline = MX_TIME_INFINITE;
  else
    mx_deadline = mx::deadline_after(timeout.ToNanoseconds());

  mx_signals_t pending = 0u;
  while (!ready_) {
    mx_status_t status =
        fence_.wait_one(kFenceSignalledOrClosed, mx_deadline, &pending);
    FTL_DCHECK(status == MX_OK || status == MX_ERR_TIMED_OUT);
    ready_ = pending & kFenceSignalledOrClosed;
    if (mx_deadline != MX_TIME_INFINITE)
      break;
  }
  return ready_;
}

void AcquireFence::WaitReadyAsync(ftl::Closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FTL_DCHECK(!ready_callback_);

  if (ready_) {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(ready_callback));
    return;
  }

  // Returned key will always be non-zero.
  FTL_DCHECK(handler_key_ == 0);
  handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, fence_.get(), kFenceSignalledOrClosed);
  ready_callback_ = std::move(ready_callback);
}

void AcquireFence::ClearHandler() {
  if (handler_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
    handler_key_ = 0u;
  }
}

void AcquireFence::OnHandleReady(mx_handle_t handle,
                                 mx_signals_t pending,
                                 uint64_t count) {
  FTL_DCHECK(handle == fence_.get());
  FTL_DCHECK(pending & kFenceSignalledOrClosed);
  FTL_DCHECK(ready_callback_);

  // TODO: Handle the case where there is an error condition, probably want to
  // close the session.

  ready_ = true;
  ftl::Closure callback = std::move(ready_callback_);
  ClearHandler();

  callback();
}

}  // namespace scene_manager
