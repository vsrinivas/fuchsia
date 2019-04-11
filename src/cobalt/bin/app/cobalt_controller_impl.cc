// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_controller_impl.h"

#include <memory>
#include <mutex>
#include <vector>

#include "third_party/cobalt/logger/local_aggregation.pb.h"

namespace cobalt {

using encoder::ShippingManager;

CobaltControllerImpl::CobaltControllerImpl(
    async_dispatcher_t* dispatcher,
    std::vector<encoder::ShippingManager*> shipping_managers,
    logger::EventAggregator* event_aggregator,
    encoder::ObservationStore* observation_store)
    : dispatcher_(dispatcher),
      shipping_managers_(std::move(shipping_managers)),
      event_aggregator_(event_aggregator),
      observation_store_(observation_store) {
  CHECK(dispatcher_);
  CHECK(event_aggregator_);
  CHECK(observation_store_);
  for (auto shipping_manager : shipping_managers_) {
    CHECK(shipping_manager);
  }
}

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

void CobaltControllerImpl::GenerateAggregatedObservations(
    uint32_t day_index, GenerateAggregatedObservationsCallback callback) {
  size_t num_obs_before = observation_store_->num_observations_added();
  event_aggregator_->GenerateObservationsNoWorker(day_index);
  size_t num_new_obs =
      observation_store_->num_observations_added() - num_obs_before;
  callback(num_new_obs);
}

}  // namespace cobalt
