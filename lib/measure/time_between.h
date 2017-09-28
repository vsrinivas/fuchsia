// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_TIME_BETWEEN_H_
#define GARNET_LIB_MEASURE_TIME_BETWEEN_H_

#include <map>
#include <stack>
#include <unordered_map>
#include <vector>

#include "garnet/lib/measure/event_spec.h"
#include "lib/fxl/macros.h"
#include "zircon/system/ulib/trace-reader/include/trace-reader/reader.h"

namespace tracing {
namespace measure {

// Indicates whether the beginning or an end of the trace event is targeted.
enum class Anchor { Begin, End };

// A "time_between" measurement targets two events and measures time between
// their consecutive occurences.
struct TimeBetweenSpec {
  uint64_t id;
  EventSpec first_event;
  Anchor first_anchor;
  EventSpec second_event;
  Anchor second_anchor;
};

class MeasureTimeBetween {
 public:
  explicit MeasureTimeBetween(std::vector<TimeBetweenSpec> specs);

  // Processes a recorded trace event. Returns true on success and false if the
  // record was ignored due to an error in the provided data. Trace events must
  // be processed in non-decreasing order of timestamps.
  bool Process(const trace::Record::Event& event);

  // Returns the results of the measurements. The results are represented as a
  // map of measurement ids to lists of time deltas representing the performed
  // measurements.
  //
  // For each pair of a first event occurence followed by a second event
  // occurence (with no other occurences of either event in between), the
  // results contain the duration between the two.
  const std::unordered_map<uint64_t, std::vector<trace_ticks_t>>& results() {
    return results_;
  }

 private:
  bool ProcessInstant(const trace::Record::Event& event);

  void AddResult(uint64_t spec_id, trace_ticks_t from, trace_ticks_t to);

  std::vector<TimeBetweenSpec> specs_;
  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> results_;

  // Maps ids of "time between" measurements to the timestamp of the most recent
  // occurence of the first event.
  std::unordered_map<uint64_t, trace_ticks_t> pending_time_between_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MeasureTimeBetween);
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_TIME_BETWEEN_H_
