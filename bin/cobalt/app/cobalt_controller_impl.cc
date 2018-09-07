// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_controller_impl.h"

#include "lib/fxl/functional/make_copyable.h"

namespace cobalt {

using encoder::ShippingManager;

CobaltControllerImpl::CobaltControllerImpl(async_dispatcher_t* dispatcher,
                                           ShippingManager* shipping_manager)
    : dispatcher_(dispatcher), shipping_manager_(shipping_manager) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // invokes |callback| on the main thread
  shipping_manager_->RequestSendSoon(
      fxl::MakeCopyable([dispatcher = dispatcher_,
                         callback = std::move(callback)](bool success) mutable {
        async::PostTask(dispatcher, [callback = std::move(callback), success] {
          callback(success);
        });
      }));
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  shipping_manager_->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  callback();
}

void CobaltControllerImpl::GetNumSendAttempts(
    GetNumSendAttemptsCallback callback) {
  callback(shipping_manager_->num_send_attempts());
}

void CobaltControllerImpl::GetFailedSendAttempts(
    GetFailedSendAttemptsCallback callback) {
  callback(shipping_manager_->num_failed_attempts());
}
}  // namespace cobalt
