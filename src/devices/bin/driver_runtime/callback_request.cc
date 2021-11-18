// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/callback_request.h"

#include "src/devices/bin/driver_runtime/dispatcher.h"

namespace driver_runtime {

// static
void CallbackRequest::QueueOntoDispatcher(std::unique_ptr<CallbackRequest> req) {
  ZX_ASSERT(req->callback_);
  ZX_ASSERT(req->dispatcher_);
  req->dispatcher_->QueueCallback(std::move(req));
}

}  // namespace driver_runtime
