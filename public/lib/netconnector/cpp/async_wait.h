// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// Manages a single async wait.
class AsyncWait {
 public:
  // Constructs an AsyncWait.
  AsyncWait(const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());

  // Calls |Cancel| and destructs this AsyncWait.
  ~AsyncWait();

  // Starts waiting for |signals| on |handle|. Cannot be called if this
  // AsyncWait is currently waiting.
  void Start(mx_handle_t handle,
             mx_signals_t signals,
             mx_time_t timeout,
             const std::function<void()> callback);

  // Cancels a wait in progress if there is one in progress, otherwise does
  // nothing.
  void Cancel();

  bool is_waiting() const { return wait_id_ != 0; }
  mx_status_t status() const { return status_; }
  mx_signals_t pending() const { return pending_; }

 private:
  static void CallbackHandler(mx_status_t status,
                              mx_signals_t pending,
                              void* closure);

  const FidlAsyncWaiter* waiter_;
  std::function<void()> callback_;
  FidlAsyncWaitID wait_id_;
  mx_status_t status_;
  mx_signals_t pending_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AsyncWait);
};

}  // namespace netconnector
