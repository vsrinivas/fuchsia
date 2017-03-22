// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_RESULTS_OUTPUT_H_
#define APPS_TRACING_SRC_TRACE_RESULTS_OUTPUT_H_

#include <ostream>
#include <unordered_map>
#include <vector>

#include "apps/tracing/src/trace/spec.h"

namespace tracing {

void OutputResults(
    std::ostream& out,
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& samples);

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_RESULTS_OUTPUT_H_
