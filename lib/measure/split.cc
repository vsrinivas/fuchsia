// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/split.h"

namespace tracing {
namespace measure {

std::vector<SampleRange> Split(const std::vector<Ticks>& samples,
                               std::vector<size_t> split_samples_at) {
  if (samples.empty()) {
    return {};
  }

  // Discard separators that are out-of-range and ensure that there is a final
  // separator corresponding to the end of the last range.
  while (!split_samples_at.empty() &&
         split_samples_at.back() >= samples.size()) {
    split_samples_at.pop_back();
  }
  split_samples_at.push_back(samples.size());

  std::vector<SampleRange> results;
  auto begin = samples.begin();
  for (size_t end_index : split_samples_at) {
    auto end = samples.begin() + end_index;
    std::vector<uint64_t> sample_section(begin, end);
    results.push_back({sample_section,
                       static_cast<size_t>(begin - samples.begin()),
                       end_index});
    begin = end;
  }
  return results;
}

}  // namespace measure
}  // namespace tracing
