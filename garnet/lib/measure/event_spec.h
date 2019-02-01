// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_EVENT_SPEC_H_
#define GARNET_LIB_MEASURE_EVENT_SPEC_H_

#include <stdlib.h>
#include <string>

#include <trace-reader/reader.h>

namespace tracing {
namespace measure {

// Specifies a trace event.
struct EventSpec {
  fbl::String name;
  fbl::String category;
};

// Parameters for requested measurements that are common across all
// measurement types.
struct MeasurementSpecCommon {
  MeasurementSpecCommon() : id(0) {}
  MeasurementSpecCommon(uint64_t id) : id(id) {}

  uint64_t id;
  // The test name/label to use in the output perf results JSON file.
  std::string output_test_name;
  // Whether the first run should be recorded separately.
  bool split_first = false;
  // The number of expected samples for the measurement.
  size_t expected_sample_count = 0;
};

bool EventMatchesSpec(const trace::Record::Event& event, const EventSpec& spec);

std::ostream& operator<<(std::ostream& os, EventSpec event_spec);

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_EVENT_SPEC_H_
