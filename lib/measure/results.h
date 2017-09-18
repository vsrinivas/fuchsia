// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_MEASURE_RESULTS_H_
#define APPS_TRACING_LIB_MEASURE_RESULTS_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/tracing/lib/measure/measurements.h"
#include "apps/tracing/lib/trace/ticks.h"

namespace tracing {
namespace measure {

// A group of recorded samples.
struct SampleGroup {
  std::vector<double> values;
  std::string label;
};

// Result of a single measurement.
struct Result {
  std::vector<SampleGroup> samples;
  std::string unit;
  std::string label;
};

// Computes the results of a benchmark from the measurement spec and the raw
// ticks.
std::vector<Result> ComputeResults(
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& ticks,
    uint64_t ticks_per_second);

}  // namespace measure
}  // namespace tracing

#endif  // APPS_TRACING_LIB_MEASURE_RESULTS_H_
