// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_controller_impl.h"

#include "lib/fxl/functional/make_copyable.h"

namespace cobalt {

using encoder::ShippingDispatcher;

CobaltControllerImpl::CobaltControllerImpl(
    async_t* async, ShippingDispatcher* shipping_dispatcher)
    : async_(async), shipping_dispatcher_(shipping_dispatcher) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // invokes |callback| on the main thread
  shipping_dispatcher_->RequestSendSoon(fxl::MakeCopyable(
      [ async = async_, callback = std::move(callback) ](bool success) mutable {
        async::PostTask(async, [ callback = std::move(callback), success ] {
          callback(success);
        });
      }));
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  shipping_dispatcher_->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  callback();
}

void CobaltControllerImpl::NumSendAttempts(NumSendAttemptsCallback callback) {
  callback(shipping_dispatcher_->NumSendAttempts());
}

void CobaltControllerImpl::FailedSendAttempts(
    FailedSendAttemptsCallback callback) {
  callback(shipping_dispatcher_->NumFailedAttempts());
}
}  // namespace cobalt
