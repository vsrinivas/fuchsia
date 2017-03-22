// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_MEASURE_SPLIT_H_
#define APPS_TRACING_LIB_MEASURE_SPLIT_H_

#include <vector>

#include "apps/tracing/lib/trace/ticks.h"

namespace tracing {
namespace measure {

// Represents a consecutive range of recorded samples.
struct SampleRange {
  std::vector<Ticks> samples;
  // Inclusive.
  size_t begin;
  // Non-inclusive.
  size_t end;
};

// Splits the recorded samples into consecutive ranges.
//
// |split_samples_at| must be a list of strictly-increasing, non-negative
// indices corresponding to non-inclusive ends of each subsequent range.
std::vector<SampleRange> Split(const std::vector<Ticks>& samples,
                               std::vector<size_t> split_samples_at);

}  // namespace measure
}  // namespace tracing

#endif  // APPS_TRACING_LIB_MEASURE_SPLIT_H_
