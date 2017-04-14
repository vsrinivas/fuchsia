// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_SPEC_H_
#define APPS_TRACING_SRC_TRACE_SPEC_H_

#include "apps/tracing/lib/measure/duration.h"
#include "apps/tracing/lib/measure/measurements.h"
#include "apps/tracing/lib/measure/time_between.h"

#include <string>
#include <vector>

#include "lib/ftl/time/time_delta.h"

namespace tracing {

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
  measure::Measurements measurements;
};

bool DecodeSpec(const std::string& json, Spec* spec);

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_SPEC_H_
