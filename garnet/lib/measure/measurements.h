// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_MEASUREMENTS_H_
#define GARNET_LIB_MEASURE_MEASUREMENTS_H_

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
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_MEASUREMENTS_H_
