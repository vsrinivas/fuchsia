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

namespace driver_runtime {

class CallbackRequest;
using Callback = fit::inline_callback<void(std::unique_ptr<CallbackRequest>, fdf_status_t),
                                      sizeof(fbl::RefPtr<Object>)>;

// Wraps a callback so that it can be added to a list.
class CallbackRequest : public fbl::DoublyLinkedListable<std::unique_ptr<CallbackRequest>> {
 public:
  CallbackRequest() = default;

  // Sets the callback that will be called by |Call|.
  void SetCallback(Callback callback, fdf_status_t callback_reason) {
    ZX_ASSERT(!callback_);
    ZX_ASSERT(!reason_);
    callback_ = std::move(callback);
    reason_ = callback_reason;
  }

  // Calls the callback, returning ownership of the request back the original requester,
  void Call(std::unique_ptr<CallbackRequest> callback_request, fdf_status_t status) {
    ZX_ASSERT(reason_);
    // If no particular callback reason was set, we will use the status provided by the dispatcher.
    if (*reason_ != ZX_OK) {
      status = *reason_;
    }
    reason_ = std::nullopt;
    callback_(std::move(callback_request), status);
  }

  // Returns whether a callback has been set via |SetCallback| and not yet been called.
  bool IsPending() { return !!callback_; }

 private:
  Callback callback_;
  // Reason for scheduling the callback.
  std::optional<fdf_status_t> reason_;
};

}  // namespace driver_runtime

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_CALLBACK_REQUEST_H_
