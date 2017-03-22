// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_SPEC_H_
#define APPS_TRACING_SRC_TRACE_SPEC_H_

#include "apps/tracing/lib/measure/duration.h"
#include "apps/tracing/lib/measure/time_between.h"

#include <string>
#include <vector>

#include "lib/ftl/time/time_delta.h"

namespace tracing {

// Description of measurements to be performed on the captured traces.
struct Measurements {
  std::vector<measure::DurationSpec> duration;
  std::vector<measure::TimeBetweenSpec> time_between;

  // Maps measurement ids indices at which the samples recorded for a
  // measurement must be split into consecutive ranges and reported separately.
  std::unordered_map<uint64_t, std::vector<size_t>> split_samples_at;
};

// Tracing specification.
struct Spec {
  // Url of the application to be run.
  std::string app;

  // Startup arguments passed to the application.
  std::vector<std::string> args;

  // Tracing categories enabled when tracing the application.
  std::vector<std::string> categories;

  // Duration of the benchmark.
  ftl::TimeDelta duration = ftl::TimeDelta::FromSeconds(10);

  // Measurements to be performed on the captured traces.
  Measurements measurements;
};

bool DecodeSpec(const std::string& json, Spec* spec);

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_SPEC_H_
