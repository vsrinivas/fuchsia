// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/buffers/cpp/buffer_fence.h"

#include <mx/time.h>
#include "lib/ftl/logging.h"

namespace mozart {
namespace {

constexpr mx_status_t kSignaledOrClosed =
    MX_EPAIR_SIGNALED | MX_EPAIR_PEER_CLOSED;
}

BufferFence::BufferFence(mx::eventpair fence) : fence_(std::move(fence)) {
  FTL_DCHECK(fence_);
}

BufferFence::~BufferFence() {
  ClearReadyCallback();
}

bool BufferFence::WaitReady(ftl::TimeDelta timeout) {
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
        fence_.wait_one(kSignaledOrClosed, mx_deadline, &pending);
    FTL_DCHECK(status == MX_OK || status == MX_ERR_TIMED_OUT);
    if (pending & kSignaledOrClosed)
      ready_ = true;
    if (mx_deadline != MX_TIME_INFINITE)
      break;
  }
  return ready_;
}

void BufferFence::SetReadyCallback(ftl::Closure ready_callback) {
  ClearReadyCallback();
  if (!ready_callback)
    return;

  if (ready_) {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(ready_callback));
    return;
  }

  handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(this, fence_.get(),
                                                            kSignaledOrClosed);
  ready_callback_ = std::move(ready_callback);
}

void BufferFence::ClearReadyCallback() {
  if (ready_callback_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
    ready_callback_ = ftl::Closure();
  }
}

void BufferFence::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(handle == fence_.get());
  FTL_DCHECK(pending & kSignaledOrClosed);
  FTL_DCHECK(ready_callback_);

  ready_ = true;
  ftl::Closure callback = std::move(ready_callback_);
  ClearReadyCallback();

  callback();
}

}  // namespace mozart
