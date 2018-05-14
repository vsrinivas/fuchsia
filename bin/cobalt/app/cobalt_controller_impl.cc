// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_controller_impl.h"

namespace cobalt {

using encoder::ShippingDispatcher;

CobaltControllerImpl::CobaltControllerImpl(
    async_t* async, ShippingDispatcher* shipping_dispatcher)
    : async_(async), shipping_dispatcher_(shipping_dispatcher) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // callback_adapter invokes |callback| on the main thread.
  std::function<void(bool)> callback_adapter = [this, callback](bool success) {
    async::PostTask(async_, [callback, success]() { callback(success); });
  };
  shipping_dispatcher_->RequestSendSoon(callback_adapter);
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
