// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_CALLBACK_REQUEST_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_CALLBACK_REQUEST_H_

#include <lib/fdf/types.h>
#include <lib/fit/function.h>

#include <optional>

#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_runtime/object.h"

// Defined in "src/devices/bin/driver_runtime/dispatcher.h"
struct fdf_dispatcher;

namespace driver_runtime {

class CallbackRequest;
using Callback =
    fit::inline_callback<void(std::unique_ptr<CallbackRequest>, fdf_status_t), sizeof(void*) * 2>;

// Wraps a callback so that it can be added to a list.
class CallbackRequest : public fbl::DoublyLinkedListable<std::unique_ptr<CallbackRequest>> {
 public:
  CallbackRequest() = default;

  // Initializes the callback to be queued.
  // Sets the dispatcher, and the callback that will be called by |Call|.
  // |async_operation| is the async_dispatcher_t operation that this callback request manages.
  void SetCallback(struct fdf_dispatcher* dispatcher, Callback callback,
                   void* async_operation = nullptr) {
    ZX_ASSERT(!dispatcher_);
    ZX_ASSERT(!callback_);
    ZX_ASSERT(!async_operation_);
    dispatcher_ = dispatcher;
    callback_ = std::move(callback);
    if (async_operation) {
      async_operation_ = async_operation;
    }
  }

  // Calls the callback, returning ownership of the request back the original requester,
  void Call(std::unique_ptr<CallbackRequest> callback_request, fdf_status_t status) {
    // If no particular callback reason was set, we will use the status provided by the dispatcher.
    if (reason_.has_value() && (*reason_ != ZX_OK)) {
      status = *reason_;
    }
    dispatcher_ = nullptr;
    reason_ = std::nullopt;
    async_operation_ = std::nullopt;
    callback_(std::move(callback_request), status);
  }

  void SetCallbackReason(fdf_status_t callback_reason) { reason_ = callback_reason; }

  // Returns whether a callback has been set via |SetCallback| and not yet been called.
  bool IsPending() { return !!callback_; }

  // Clears the callback request state.
  void Reset() {
    dispatcher_ = nullptr;
    reason_ = std::nullopt;
    async_operation_ = std::nullopt;
    callback_ = nullptr;
  }

  // Returns whether this callback manages an async_dispatcher_t operation.
  bool has_async_operation() const { return async_operation_.has_value() && *async_operation_; }

  // Returns whether this callback manages |operation|.
  bool holds_async_operation(void* operation) const {
    return async_operation_.has_value() && *async_operation_ == operation;
  }

 private:
  struct fdf_dispatcher* dispatcher_;
  Callback callback_;
  // Reason for scheduling the callback.
  std::optional<fdf_status_t> reason_;
  // The async_dispatcher_t operation that this callback request is wrapping around.
  std::optional<void*> async_operation_;
};

}  // namespace driver_runtime

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_CALLBACK_REQUEST_H_
