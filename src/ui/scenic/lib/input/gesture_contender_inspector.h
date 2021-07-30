// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_INSPECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_INSPECTOR_H_

#include <lib/inspect/cpp/inspect.h>

#include <deque>
#include <unordered_map>

namespace scenic_impl::input {

// Utility that gesture contenders use to send diagnostics to Inspect.
// Example inspect output:
//
// Last 10 minutes of injected events:
//  Events at minute 0:
//     View 44907:
//       num_injected_events = 74
//       num_lost_streams = 0
//       num_won_streams = 2
//   Events at minute 1:
//     View 44907:
//       num_injected_events = 133
//       num_lost_streams = 0
//       num_won_streams = 6
//     View 200884:
//       num_injected_events = 72
//       num_lost_streams = 0
//       num_won_streams = 0
//   Sum:
//     num_injected_events = 279
//     num_lost_streams = 0
//     num_won_streams = 8
//
class GestureContenderInspector {
 public:
  explicit GestureContenderInspector(inspect::Node inspect_node);

  void OnInjectedEvents(zx_koid_t view_ref_koid, uint64_t num_events);
  void OnContestDecided(zx_koid_t view_ref_koid, bool won);

  // How long to track injection history.
  static constexpr uint64_t kNumMinutesOfHistory = 10;

 private:
  struct ViewHistory {
    uint64_t num_injected_events = 0;
    uint64_t num_won_streams = 0;
    uint64_t num_lost_streams = 0;
  };

  struct InspectHistory {
    // The minute this was recorded during. Used as the key for appending new values.
    uint64_t minute_key = 0;
    // Per-view data during |minute_key|.
    std::unordered_map<zx_koid_t, ViewHistory> per_view_data;
  };

  void UpdateHistory(zx::time now);
  void ReportStats(inspect::Inspector& inspector) const;
  std::unordered_map<zx_koid_t, GestureContenderInspector::ViewHistory>& GetCurrentInspectHistory();

  inspect::Node node_;
  inspect::LazyNode history_stats_node_;

  std::deque<InspectHistory> history_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_INSPECTOR_H_
