// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_controller_impl.h"

#include <memory>
#include <vector>

namespace cobalt {

using encoder::ShippingManager;

CobaltControllerImpl::CobaltControllerImpl(
    async_dispatcher_t* dispatcher, encoder::ShippingManager* shipping_manager,
    local_aggregation::EventAggregatorManager* event_aggregator_mgr,
    observation_store::ObservationStore* observation_store)
    : dispatcher_(dispatcher),
      shipping_manager_(shipping_manager),
      event_aggregator_mgr_(event_aggregator_mgr),
      observation_store_(observation_store) {
  CHECK(dispatcher_);
  CHECK(event_aggregator_mgr_);
  CHECK(observation_store_);
  CHECK(shipping_manager_);
}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // A lambda with a captured by move non-copyable parameter can not be converted to a
  // std::function, so wrap the callback in a copyable shared_ptr.
  std::shared_ptr<RequestSendSoonCallback> copyable_callback =
      std::make_shared<RequestSendSoonCallback>(std::move(callback));
  shipping_manager_->RequestSendSoon([copyable_callback,
                                      dispatcher = dispatcher_](bool success) mutable {
    // invokes |callback| on the main thread
    async::PostTask(dispatcher,
                    [callback = std::move(*copyable_callback), success]() { callback(success); });
  });
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  shipping_manager_->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  callback();
}

void CobaltControllerImpl::GetNumSendAttempts(GetNumSendAttemptsCallback callback) {
  callback(shipping_manager_->num_send_attempts());
}

void CobaltControllerImpl::GetFailedSendAttempts(GetFailedSendAttemptsCallback callback) {
  callback(shipping_manager_->num_failed_attempts());
}

void CobaltControllerImpl::GetNumObservationsAdded(GetNumObservationsAddedCallback callback) {
  callback(observation_store_->num_observations_added());
}

void CobaltControllerImpl::GenerateAggregatedObservations(
    uint32_t day_index, std::vector<uint32_t> report_ids,
    GenerateAggregatedObservationsCallback callback) {
  std::vector<uint64_t> num_obs_before =
      observation_store_->num_observations_added_for_reports(report_ids);
  event_aggregator_mgr_->GenerateObservationsNoWorker(day_index);
  std::vector<uint64_t> num_obs_after =
      observation_store_->num_observations_added_for_reports(report_ids);
  std::vector<uint64_t> num_new_obs;
  for (size_t i = 0; i < report_ids.size(); i++) {
    num_new_obs.push_back(num_obs_after[i] - num_obs_before[i]);
  }
  callback(num_new_obs);
}

}  // namespace cobalt
