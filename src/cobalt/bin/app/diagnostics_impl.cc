// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/diagnostics_impl.h"

#include <lib/inspect/cpp/inspect.h>

#include <chrono>

namespace cobalt {

DiagnosticsImpl::DiagnosticsImpl(inspect::Node node) : node_(std::move(node)) {
  // Diagnostics for sending observations to Clearcut.
  send_observations_ = node_.CreateChild("sending_observations");
  send_observations_successes_ = send_observations_.CreateInt("successes", 0);
  send_observations_errors_ = send_observations_.CreateInt("errors", 0);
  last_successful_send_time_ = send_observations_.CreateInt("last_success_time", 0);
  last_send_error_time_ = send_observations_.CreateInt("last_error_time", 0);
  last_send_error_code_ = send_observations_.CreateInt("last_error_code", 0);
  last_send_error_message_ = send_observations_.CreateString("last_error_message", "");
  last_send_error_details_ = send_observations_.CreateString("last_error_details", "");

  // Diagnostics for stored observations.
  stored_observations_ = node_.CreateChild("observations_stored");
  stored_observations_total_ = stored_observations_.CreateInt("total", 0);
  stored_observations_byte_count_ = stored_observations_.CreateInt("byte_count", 0);
  stored_observations_byte_count_limit_ = stored_observations_.CreateInt("byte_count_limit", 0);
}

void DiagnosticsImpl::SentObservationResult(const cobalt::util::Status& status) {
  time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  if (status.ok()) {
    send_observations_successes_.Add(1);
    last_successful_send_time_.Set(current_time);
  } else {
    send_observations_errors_.Add(1);
    last_send_error_time_.Set(current_time);
    last_send_error_code_.Set(status.error_code());
    last_send_error_message_.Set(status.error_message());
    last_send_error_details_.Set(status.error_details());
  }
}

void DiagnosticsImpl::ObservationStoreUpdated(
    const std::map<ReportSpec, uint64_t>& num_obs_per_report, int64_t store_byte_count,
    int64_t max_store_bytes) {
  uint64_t total_observations = 0;
  for (const auto& [report_spec, count] : num_obs_per_report) {
    total_observations += count;
    std::string report_ids = "report_" + std::to_string(report_spec.customer_id) + "-" +
                             std::to_string(report_spec.project_id) + "-" +
                             std::to_string(report_spec.metric_id) + "-" +
                             std::to_string(report_spec.report_id);
    auto report_count = stored_observations_per_report_.find(report_ids);
    if (report_count != stored_observations_per_report_.end()) {
      report_count->second.Set(count);
    } else {
      stored_observations_per_report_[report_ids] =
          stored_observations_.CreateInt(report_ids, count);
    }
  }
  stored_observations_total_.Set(total_observations);
  stored_observations_byte_count_.Set(store_byte_count);
  stored_observations_byte_count_limit_.Set(max_store_bytes);
}

}  // namespace cobalt
