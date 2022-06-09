// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_FUCHSIA_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_FUCHSIA_H_

#include <lib/inspect/cpp/inspect.h>
#include <zircon/system/public/zircon/compiler.h>

#include <map>
#include <memory>
#include <mutex>

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_base.h"

//
// Usage:
//
//    struct Filesystem {
//      std::unique_ptr<OperationTracker> read_tracker_ = std::make_unique<OperationTrackerFuchsia>(
//        stats_node, "read", /* latency params */);
//
//      zx_status_t Read() {
//        return read_tracker_->Track([] {
//          // read impl here; return zx_status_t;
//          // if return ZX_OK, records latency
//          // otherwise records error counter
//        });
//      }
//
//    };
//

namespace fs_inspect {

struct LatencyHistogramSettings {
  // Base duration to use for latency measurements (zx::nsec(1) for nanoseconds, zx::usec(1) for
  // microseconds, etc...).
  zx::duration time_base;

  //
  // Histogram Bucket Options:
  //
  uint64_t floor;
  uint64_t initial_step;
  uint64_t step_multiplier;
  size_t buckets;
};

// **WARNING**: The values set below must match the metric definitions in Cobalt (metric ID = 70).
constexpr LatencyHistogramSettings kNodeOperationHistogramSettings{
    .time_base = zx::usec(1),
    .floor = 0,
    .initial_step = 5,
    .step_multiplier = 2,
    .buckets = 16,
};

/// Provides RAII-style tracking of filesystem operation latency and errors.
///
/// Attaches a child node to the given parent passed to the constructor with the following layout:
///
///     [operation_name]:
///       latency = [latency histogram of successful operations]
///       total = [running total of all operations]
///       ok = [running total of successful operations]
///       fail = [running total of failed operations]
///       errors: [created dynamically on first non-ok return of operation]
///         ZX_ERR_ACCESS_DENIED = 5,   [child properties created when errors encountered]
///
class OperationTrackerFuchsia final : public OperationTracker {
 public:
  static constexpr char kOkCountName[] = "ok";
  static constexpr char kFailCountName[] = "fail";
  static constexpr char kTotalCountName[] = "total";
  static constexpr char kLatencyHistogramName[] = "latency";
  static constexpr char kErrorNodeName[] = "errors";

  /// Used to record latency/error metrics for an arbitrary filesystem operation.
  OperationTrackerFuchsia(inspect::Node& root_node, std::string_view operation_name,
                          LatencyHistogramSettings histogram_settings);

 private:
  void OnSuccess(zx::duration latency) override;
  void OnError(zx_status_t error) override;
  void OnError() override;

  inspect::Node operation_node_;
  inspect::UintProperty ok_counter_;
  inspect::UintProperty fail_counter_;
  inspect::UintProperty total_counter_;
  zx::duration latency_base_unit_;
  inspect::ExponentialUintHistogram latency_histogram_;
  std::mutex error_mutex_;
  inspect::Node error_node_ __TA_GUARDED(error_mutex_){};
  std::map<zx_status_t, inspect::UintProperty> error_map_ __TA_GUARDED(error_mutex_){};
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_FUCHSIA_H_
