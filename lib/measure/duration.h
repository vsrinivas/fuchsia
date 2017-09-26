// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_DURATION_H_
#define GARNET_LIB_MEASURE_DURATION_H_

#include <map>
#include <stack>
#include <unordered_map>
#include <vector>

#include "garnet/lib/measure/event_spec.h"
#include "garnet/lib/trace/reader.h"
#include "lib/fxl/macros.h"

namespace tracing {
namespace measure {

// A "duration" measurement specifies a single trace event. The target event can
// be recorded as a "duration" or as an "async" event.
struct DurationSpec {
  uint64_t id;
  EventSpec event;
};

class MeasureDuration {
 public:
  explicit MeasureDuration(std::vector<DurationSpec> specs);

  // Processes a recorded trace event. Returns true on success and false if the
  // record was ignored due to an error in the provided data. Trace events must
  // be processed in non-decreasing order of timestamps.
  bool Process(const reader::Record::Event& event);

  // Returns the results of the measurements. The results are represented as a
  // map of measurement ids to lists of time deltas representing the durations
  // of the matching trace events.
  const std::unordered_map<uint64_t, std::vector<Ticks>>& results() {
    return results_;
  }

 private:
  bool ProcessAsyncStart(const reader::Record::Event& event);
  bool ProcessAsyncEnd(const reader::Record::Event& event);
  bool ProcessDurationStart(const reader::Record::Event& event);
  bool ProcessDurationEnd(const reader::Record::Event& event);

  void AddResult(uint64_t spec_id, Ticks from, Ticks to);

  std::vector<DurationSpec> specs_;
  std::unordered_map<uint64_t, std::vector<Ticks>> results_;

  // Async event ids are scoped to names. To match "end" events
  // with "begin" events, we keep a map of unmatched begin events.
  struct PendingAsyncKey {
    std::string category;
    std::string name;
    uint64_t id;

    bool operator<(const PendingAsyncKey& other) const;
  };
  std::map<PendingAsyncKey, Ticks> pending_async_begins_;

  // Duration events recorded on a thread can be nested. To match "end" events
  // with "begin" events, we keep a per-thread stack of timestamps of unmatched
  // "begin" events.
  std::map<ProcessThread, std::stack<Ticks>> duration_stacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MeasureDuration);
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_DURATION_H_
