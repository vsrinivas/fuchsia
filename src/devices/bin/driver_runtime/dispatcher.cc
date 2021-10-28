// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/dispatcher.h"

// This is currently a bare-bones implementation for testing.

namespace driver_runtime {

// static
fdf_status_t Dispatcher::Create(uint32_t options, const char* scheduler_role,
                                size_t scheduler_role_len, bool use_async_loop,
                                std::unique_ptr<Dispatcher>* out_dispatcher) {
  bool unsynchronized = options & FDF_DISPATCHER_OPTION_UNSYNCHRONIZED;
  bool allow_sync_calls = options & FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
  if (unsynchronized && allow_sync_calls) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto dispatcher = std::make_unique<Dispatcher>(unsynchronized, allow_sync_calls, use_async_loop);
  *out_dispatcher = std::move(dispatcher);
  return ZX_OK;
}
void Dispatcher::QueueCallback(std::unique_ptr<CallbackRequest> callback_request) {
  if (!use_async_loop_) {
    callback_request->Call(std::move(callback_request), ZX_OK);
    return;
  }

  fbl::AutoLock lock(&callback_lock_);
  callback_queue_.push_back(std::move(callback_request));
}

std::unique_ptr<CallbackRequest> Dispatcher::CancelCallback(CallbackRequest& callback_request) {
  fbl::AutoLock lock(&callback_lock_);
  return callback_queue_.erase(callback_request);
}

}  // namespace driver_runtime
