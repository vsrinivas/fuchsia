// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_MEASUREMENTS_H_
#define GARNET_LIB_MEASURE_MEASUREMENTS_H_

#include <unordered_map>
#include <vector>

#include "garnet/lib/measure/argument_value.h"
#include "garnet/lib/measure/duration.h"
#include "garnet/lib/measure/time_between.h"

namespace tracing {
namespace measure {

// Description of measurements to be performed on trace events.
struct Measurements {
  std::vector<measure::DurationSpec> duration;
  std::vector<measure::ArgumentValueSpec> argument_value;
  std::vector<measure::TimeBetweenSpec> time_between;

  // Maps measurement ids to numbers indicating the samples at which the
  // recorded results must be split into consecutive sample groups.
  std::unordered_map<uint64_t, std::vector<size_t>> split_samples_at;

  // Maps measurement ids to number of expected samples for these measurements.
  std::unordered_map<uint64_t, size_t> expected_sample_count;
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_MEASUREMENTS_H_
