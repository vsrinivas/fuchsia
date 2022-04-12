// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

namespace fs_inspect {

OperationTracker::OperationTracker(inspect::Node& root_node, std::string_view operation_name,
                                   LatencyHistogramSettings histogram_settings)
    : operation_node_(root_node.CreateChild(operation_name)),
      ok_counter_(operation_node_.CreateUint(kOkCountName, 0u)),
      fail_counter_(operation_node_.CreateUint(kFailCountName, 0u)),
      total_counter_(operation_node_.CreateUint(kTotalCountName, 0u)),
      latency_histogram_(operation_node_.CreateExponentialUintHistogram(
          kLatencyHistogramName, histogram_settings.floor, histogram_settings.initial_step,
          histogram_settings.step_multiplier, histogram_settings.buckets)) {}

OperationTracker::TrackerEvent OperationTracker::NewEvent() {
  return OperationTracker::TrackerEvent(this);
}

zx_status_t OperationTracker::Track(const std::function<zx_status_t()>& operation) {
  auto tracker = NewEvent();
  zx_status_t result = operation();
  tracker.SetStatus(result);
  return result;
}

void OperationTracker::OnSuccess(uint64_t latency_ns) {
  latency_histogram_.Insert(latency_ns);
  ok_counter_.Add(1u);
  total_counter_.Add(1u);
}

void OperationTracker::OnError(zx_status_t error) {
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

void OperationTracker::OnError() {
  fail_counter_.Add(1u);
  total_counter_.Add(1u);
}

OperationTracker::TrackerEvent::TrackerEvent(OperationTracker* tracker)
    : tracker_(tracker), start_(zx::clock::get_monotonic()), status_(std::nullopt) {
  ZX_ASSERT(tracker != nullptr);
}

OperationTracker::TrackerEvent::TrackerEvent(TrackerEvent&& other) noexcept
    : tracker_(std::exchange(other.tracker_, nullptr)),
      start_(other.start_),
      status_(other.status_) {}

OperationTracker::TrackerEvent::~TrackerEvent() {
  // Handle case where object was moved from.
  if (!tracker_) {
    return;
  }

  ZX_DEBUG_ASSERT(status_.has_value());
  if (!status_.has_value()) {
    FX_LOGS(ERROR) << "TrackerEvent was destroyed without setting status!";
    // Status was not set, assume operation failed. Increment error/total operation counts.
    tracker_->OnError();
  } else if (status_.value() == ZX_OK) {
    // Operation succeeded, record latency and increment ok/total operation counts.
    uint64_t latency = static_cast<uint64_t>((zx::clock::get_monotonic() - start_).to_nsecs());
    tracker_->OnSuccess(latency);
  } else {
    // Operation failed with a specific error code, record it and increment the error/total counts.
    tracker_->OnError(status_.value());
  }
}

void OperationTracker::TrackerEvent::SetStatus(zx_status_t status) { status_ = status; }

}  // namespace fs_inspect
