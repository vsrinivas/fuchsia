// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_controller_impl.h"

#include <memory>
#include <vector>

namespace cobalt {

using encoder::ShippingManager;

CobaltControllerImpl::CobaltControllerImpl(async_dispatcher_t* dispatcher,
                                           CobaltService* cobalt_service)
    : dispatcher_(dispatcher), cobalt_service_(cobalt_service) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // A lambda with a captured by move non-copyable parameter can not be converted to a
  // std::function, so wrap the callback in a copyable shared_ptr.
  std::shared_ptr<RequestSendSoonCallback> copyable_callback =
      std::make_shared<RequestSendSoonCallback>(std::move(callback));
  cobalt_service_->shipping_manager()->RequestSendSoon(
      [copyable_callback, dispatcher = dispatcher_](bool success) mutable {
        // invokes |callback| on the main thread
        async::PostTask(dispatcher, [callback = std::move(*copyable_callback), success]() {
          callback(success);
        });
      });
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  cobalt_service_->shipping_manager()->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  callback();
}

void CobaltControllerImpl::GetNumSendAttempts(GetNumSendAttemptsCallback callback) {
  callback(cobalt_service_->shipping_manager()->num_send_attempts());
}

void CobaltControllerImpl::GetFailedSendAttempts(GetFailedSendAttemptsCallback callback) {
  callback(cobalt_service_->shipping_manager()->num_failed_attempts());
}

void CobaltControllerImpl::GetNumObservationsAdded(GetNumObservationsAddedCallback callback) {
  callback(cobalt_service_->observation_store()->num_observations_added());
}

void CobaltControllerImpl::GenerateAggregatedObservations(
    uint32_t day_index, std::vector<uint32_t> report_ids,
    GenerateAggregatedObservationsCallback callback) {
  std::vector<uint64_t> num_obs_before =
      cobalt_service_->observation_store()->num_observations_added_for_reports(report_ids);
  cobalt_service_->event_aggregator_manager()->GenerateObservationsNoWorker(day_index);
  std::vector<uint64_t> num_obs_after =
      cobalt_service_->observation_store()->num_observations_added_for_reports(report_ids);
  std::vector<uint64_t> num_new_obs;
  for (size_t i = 0; i < report_ids.size(); i++) {
    num_new_obs.push_back(num_obs_after[i] - num_obs_before[i]);
  }
  callback(num_new_obs);
}

}  // namespace cobalt
