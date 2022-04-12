// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/clock.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/system/public/zircon/compiler.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

//
// Usage:
//
//    struct Filesystem {
//      OperationTracker read_tracker_ = OperationTracker(stats_node, "read", /* latency params */);
//
//      zx_status_t Read() {
//        return read_tracker_.Track([] {
//          // read impl here; return zx_status_t;
//          // if return ZX_OK, records latency
//          // otherwise records error counter
//        });
//      }
//
//    };
//

namespace fs_inspect {

// These default values must match the histogram definitions in Cobalt.
struct LatencyHistogramSettings {
  uint64_t floor = 0;
  uint64_t initial_step = 10000;
  uint64_t step_multiplier = 2;
  size_t buckets = 10;
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
class OperationTracker final {
 public:
  class TrackerEvent;

  static constexpr char kOkCountName[] = "ok";
  static constexpr char kFailCountName[] = "fail";
  static constexpr char kTotalCountName[] = "total";
  static constexpr char kLatencyHistogramName[] = "latency";
  static constexpr char kErrorNodeName[] = "errors";

  /// Used to record latency/error metrics for an arbitrary filesystem operation.
  OperationTracker(inspect::Node& root_node, std::string_view operation_name,
                   LatencyHistogramSettings histogram_settings = {});

  /// Record latency/error of the given operation.
  zx_status_t Track(const std::function<zx_status_t()>& operation);

  /// Create a `TrackerEvent` used to record a latency or error value. Can be moved between threads.
  /// The returned `TrackerEvent` must not outlive the associated `OperationTracker`.
  /// Time measurement starts when this object is created and ends when it goes out of scope.
  /// Use `TrackerEvent::SetStatus` to record the result of the operation.
  TrackerEvent NewEvent();

 private:
  void OnSuccess(uint64_t latency_ns);
  void OnError(zx_status_t error);
  void OnError();

  inspect::Node operation_node_;
  inspect::UintProperty ok_counter_;
  inspect::UintProperty fail_counter_;
  inspect::UintProperty total_counter_;
  inspect::ExponentialUintHistogram latency_histogram_;
  std::mutex error_mutex_;
  inspect::Node error_node_ __TA_GUARDED(error_mutex_){};
  std::map<zx_status_t, inspect::UintProperty> error_map_ __TA_GUARDED(error_mutex_){};
};

/// RAII Helper to allow automatic recording of event data when it goes out of scope.
/// __Must not__ outlive the `OperationTracker` it was created from.
class OperationTracker::TrackerEvent final {
  // Only allow TrackerEvent to be constructed by an OperationTracker.
  friend class OperationTracker;
  explicit TrackerEvent(OperationTracker* tracker);

 public:
  TrackerEvent() = delete;
  ~TrackerEvent();

  // Only allow move construction.
  TrackerEvent(TrackerEvent&&) noexcept;

  TrackerEvent(const TrackerEvent&) = delete;
  TrackerEvent& operator=(const TrackerEvent&) = delete;
  TrackerEvent& operator=(TrackerEvent&&) = delete;

  /// Set status of operation. __Must__ be called at least once before this object is destroyed.
  /// __Must__ be called from same thread that destroys this object.
  void SetStatus(zx_status_t status);

 private:
  OperationTracker* tracker_;
  const zx::time start_;
  std::optional<zx_status_t> status_;
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_
