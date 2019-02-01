// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_controller_impl.h"

#include <memory>
#include <mutex>
#include <vector>

namespace cobalt {

using encoder::ShippingManager;

CobaltControllerImpl::CobaltControllerImpl(
    async_dispatcher_t* dispatcher,
    std::vector<encoder::ShippingManager*> shipping_managers)
    : dispatcher_(dispatcher),
      shipping_managers_(std::move(shipping_managers)) {}

// This struct is used in the method RequestSendSoon() below in order
// to coordinate the results of multiple callbacks. We invoke RequestSendSoon()
// on each ShippingManager, passing in a callback that accepts a success bool.
// When each of those callbacks have completed we invoke the FIDL callback
// on the main thread with the final result which is the conjunction of each
// of the success bools.
struct RequestSendSoonCoordinator {
  explicit RequestSendSoonCoordinator(
      size_t num_to_wait_for,
      CobaltControllerImpl::RequestSendSoonCallback result_callback)
      : callbacks_waiting(num_to_wait_for),
        result_callback(std::move(result_callback)) {}
  // How many callbacks are we waiting for?
  const size_t callbacks_waiting;

  // Protects all of the rest of the values of this struct.
  std::mutex mu;

  // Incremented when a callback completes.
  size_t callbacks_completed = 0;

  // Set to the conjuction of each of the callback's results.
  bool result = true;

  // This is the FIDL callback that should be invoked with the final result.
  CobaltControllerImpl::RequestSendSoonCallback result_callback;
};

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  std::shared_ptr<RequestSendSoonCoordinator> coordinator(
      new RequestSendSoonCoordinator(shipping_managers_.size(),
                                     std::move(callback)));
  for (auto* shipping_manager : shipping_managers_) {
    shipping_manager->RequestSendSoon([coordinator,
                                       dispatcher = dispatcher_](bool s) {
      std::lock_guard<std::mutex> lock(coordinator->mu);
      coordinator->callbacks_completed++;
      coordinator->result &= s;
      if (coordinator->callbacks_completed == coordinator->callbacks_waiting) {
        // Invoke the final result callback on the main thread.
        async::PostTask(dispatcher,
                        [callback = std::move(coordinator->result_callback),
                         success = coordinator->result] { callback(success); });
      }
    });
  }
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  for (auto* shipping_manager : shipping_managers_) {
    shipping_manager->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  }
  callback();
}

void CobaltControllerImpl::GetNumSendAttempts(
    GetNumSendAttemptsCallback callback) {
  int num_send_attempts = 0;
  for (auto* shipping_manager : shipping_managers_) {
    num_send_attempts += shipping_manager->num_send_attempts();
  }
  callback(num_send_attempts);
}

void CobaltControllerImpl::GetFailedSendAttempts(
    GetFailedSendAttemptsCallback callback) {
  int num_failed_attempts = 0;
  for (auto* shipping_manager : shipping_managers_) {
    num_failed_attempts += shipping_manager->num_failed_attempts();
  }
  callback(num_failed_attempts);
}
}  // namespace cobalt
