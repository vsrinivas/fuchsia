// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_controller_impl.h"

#include <memory>
#include <vector>

#include "third_party/cobalt/src/public/lib/report_spec.h"

namespace cobalt {

CobaltControllerImpl::CobaltControllerImpl(async_dispatcher_t* dispatcher,
                                           CobaltServiceInterface* cobalt_service)
    : dispatcher_(dispatcher), cobalt_service_(cobalt_service) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // A lambda with a captured by move non-copyable parameter can not be converted to a
  // std::function, so wrap the callback in a copyable shared_ptr.
  std::shared_ptr<RequestSendSoonCallback> copyable_callback =
      std::make_shared<RequestSendSoonCallback>(std::move(callback));
  cobalt_service_->ShippingRequestSendSoon([copyable_callback,
                                            dispatcher = dispatcher_](bool success) mutable {
    // invokes |callback| on the main thread
    async::PostTask(dispatcher,
                    [callback = std::move(*copyable_callback), success]() { callback(success); });
  });
}

void CobaltControllerImpl::GenerateAggregatedObservations(
    uint32_t day_index, std::vector<fuchsia::cobalt::ReportSpec> report_specs,
    GenerateAggregatedObservationsCallback callback) {
  std::vector<lib::ReportSpec> core_report_specs;
  core_report_specs.reserve(report_specs.size());
  for (const fuchsia::cobalt::ReportSpec& report_spec : report_specs) {
    core_report_specs.push_back({.customer_id = report_spec.customer_id(),
                                 .project_id = report_spec.project_id(),
                                 .metric_id = report_spec.metric_id(),
                                 .report_id = report_spec.report_id()});
  }
  std::vector<uint64_t> num_obs_before =
      cobalt_service_->num_observations_added_for_reports(core_report_specs);
  cobalt_service_->GenerateAggregatedObservations(day_index);
  std::vector<uint64_t> num_obs_after =
      cobalt_service_->num_observations_added_for_reports(core_report_specs);
  std::vector<uint64_t> num_new_obs;
  for (size_t i = 0; i < report_specs.size(); i++) {
    num_new_obs.push_back(num_obs_after[i] - num_obs_before[i]);
  }
  callback(num_new_obs);
}

void CobaltControllerImpl::ListenForInitialized(ListenForInitializedCallback callback) {
  if (system_clock_is_accurate_) {
    callback();
    return;
  }
  accurate_clock_callbacks_.emplace_back(std::move(callback));
}

void CobaltControllerImpl::OnSystemClockBecomesAccurate() {
  system_clock_is_accurate_ = true;
  for (auto& callback : accurate_clock_callbacks_) {
    callback();
  }
  accurate_clock_callbacks_.clear();
}

}  // namespace cobalt
