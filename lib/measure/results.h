// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_RESULTS_H_
#define GARNET_LIB_MEASURE_RESULTS_H_

#include <string>
#include <unordered_map>
#include <vector>

#include <trace-engine/types.h>

#include "garnet/lib/measure/measurements.h"

namespace tracing {
namespace measure {

// A group of recorded samples.
struct SampleGroup {
  std::vector<double> values;
  std::string label;
};

// Result of a single measurement.
struct Result {
  std::vector<double> values;
  // In the old schema, values are grouped together.
  // TODO(LE-590): Get rid of it and migrate fully to the flattened new schema
  // once we make sure it works for all clients.
  std::vector<SampleGroup> samples;
  std::string unit;
  std::string label;
  std::string test_suite;
  bool split_first;
};

// Computes the results of a benchmark from the measurement spec and the raw
// recorded values.
std::vector<Result> ComputeResults(
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<uint64_t>>& recorded_values,
    uint64_t ticks_per_second);

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_RESULTS_H_
