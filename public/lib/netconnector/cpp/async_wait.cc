// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/lib/async_wait.h"

#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/logging.h"

namespace netconnector {

AsyncWait::AsyncWait(const FidlAsyncWaiter* waiter)
    : waiter_(waiter), wait_id_(0) {}

AsyncWait::~AsyncWait() {
  Cancel();
}

void AsyncWait::Start(mx_handle_t handle,
                      mx_signals_t signals,
                      mx_time_t timeout,
                      const std::function<void()> callback) {
  FTL_DCHECK(handle != MX_HANDLE_INVALID);
  FTL_DCHECK(signals != MX_SIGNAL_NONE);
  FTL_DCHECK(callback);
  FTL_DCHECK(!is_waiting());

  callback_ = callback;
  status_ = ERR_SHOULD_WAIT;
  pending_ = MX_SIGNAL_NONE;
  wait_id_ = waiter_->AsyncWait(handle, signals, timeout,
                                AsyncWait::CallbackHandler, this);
}

void AsyncWait::Cancel() {
  if (is_waiting()) {
    fidl::GetDefaultAsyncWaiter()->CancelWait(wait_id_);
    wait_id_ = 0;
    status_ = ERR_SHOULD_WAIT;
    pending_ = MX_SIGNAL_NONE;
    callback_ = nullptr;
  }
}

// static
void AsyncWait::CallbackHandler(mx_status_t status,
                                mx_signals_t pending,
                                void* closure) {
  AsyncWait* self = reinterpret_cast<AsyncWait*>(closure);
  self->wait_id_ = 0;
  self->status_ = status;
  self->pending_ = pending;
  self->callback_();
}

}  // namespace netconnector
