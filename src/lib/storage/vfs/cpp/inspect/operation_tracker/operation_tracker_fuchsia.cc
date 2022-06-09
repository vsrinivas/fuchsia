// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_fuchsia.h"

namespace fs_inspect {

OperationTrackerFuchsia::OperationTrackerFuchsia(inspect::Node& root_node,
                                                 std::string_view operation_name,
                                                 LatencyHistogramSettings histogram_settings)
    : operation_node_(root_node.CreateChild(operation_name)),
      ok_counter_(operation_node_.CreateUint(kOkCountName, 0u)),
      fail_counter_(operation_node_.CreateUint(kFailCountName, 0u)),
      total_counter_(operation_node_.CreateUint(kTotalCountName, 0u)),
      latency_base_unit_(histogram_settings.time_base),
      latency_histogram_(operation_node_.CreateExponentialUintHistogram(
          kLatencyHistogramName, histogram_settings.floor, histogram_settings.initial_step,
          histogram_settings.step_multiplier, histogram_settings.buckets)) {}

void OperationTrackerFuchsia::OnSuccess(zx::duration latency) {
  latency_histogram_.Insert(latency / latency_base_unit_);
  ok_counter_.Add(1u);
  total_counter_.Add(1u);
}

void OperationTrackerFuchsia::OnError(zx_status_t error) {
  {
    std::lock_guard guard(error_mutex_);
    if (error_map_.empty()) {
      error_node_ = operation_node_.CreateChild(kErrorNodeName);
    }
    auto found_it = error_map_.find(error);
    if (found_it != error_map_.end()) {
      found_it->second.Add(1u);
    } else {
      error_map_.insert({error, error_node_.CreateUint(zx_status_get_string(error), 1u)});
    }
  }
  OnError();
}

void OperationTrackerFuchsia::OnError() {
  fail_counter_.Add(1u);
  total_counter_.Add(1u);
}

}  // namespace fs_inspect
