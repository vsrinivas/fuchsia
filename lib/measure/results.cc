// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/results.h"

#include <sstream>

namespace tracing {
namespace measure {

namespace {

std::string GetLabel(const measure::DurationSpec& spec) {
  std::ostringstream os;
  os << spec.event.name << " (" << spec.event.category << ")";
  return os.str();
}

std::string GetLabel(const measure::TimeBetweenSpec& spec) {
  std::ostringstream os;
  os << spec.first_event.name << " (" << spec.first_event.category << ") to ";
  os << spec.second_event.name << " (" << spec.second_event.category << ")";
  return os.str();
}

std::string GetSampleGroupLabel(size_t begin, size_t end) {
  std::ostringstream os;
  os << "samples " << begin << " to " << end - 1;
  return os.str();
}

template <typename Spec>
Result ComputeSingle(Spec spec,
                     const std::vector<Ticks>& ticks,
                     std::vector<size_t> split_samples_at,
                     uint64_t ticks_per_second) {
  Result result;
  result.label = GetLabel(spec);

  // Currently we output all results in milliseconds. Later we can allow
  // measurements to specify the desired unit.
  const double ticks_to_ms = 1'000.0 / ticks_per_second;
  result.unit = "ms";

  if (ticks.empty()) {
    return result;
  }

  // Discard separators that are out of range and ensure that there is a final
  // separator corresponding to the end of the last range.
  while (!split_samples_at.empty() && split_samples_at.back() >= ticks.size()) {
    split_samples_at.pop_back();
  }
  split_samples_at.push_back(ticks.size());

  auto begin = ticks.begin();
  for (size_t end_index : split_samples_at) {
    auto end = ticks.begin() + end_index;
    SampleGroup group;
    group.label = GetSampleGroupLabel(begin - ticks.begin(), end_index);

    while (begin != end) {
      group.values.push_back(*begin * ticks_to_ms);
      begin++;
    }
    result.samples.push_back(std::move(group));
  }

  return result;
}

template <typename T>
const T& get_or_default(const std::unordered_map<uint64_t, T>& dictionary,
                        uint64_t id,
                        const T& default_value) {
  return dictionary.count(id) ? dictionary.at(id) : default_value;
}

}  // namespace

std::vector<Result> ComputeResults(
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& ticks,
    uint64_t ticks_per_second) {
  std::vector<Result> results;
  const std::vector<Ticks> no_ticks;
  const std::vector<size_t> no_split;

  for (auto& measure_spec : measurements.duration) {
    results.push_back(ComputeSingle(
        measure_spec, get_or_default(ticks, measure_spec.id, no_ticks),
        get_or_default(measurements.split_samples_at, measure_spec.id,
                       no_split),
        ticks_per_second));
  }
  for (auto& measure_spec : measurements.time_between) {
    results.push_back(ComputeSingle(
        measure_spec, get_or_default(ticks, measure_spec.id, no_ticks),
        get_or_default(measurements.split_samples_at, measure_spec.id,
                       no_split),
        ticks_per_second));
  }

  return results;
}

}  // namespace measure
}  // namespace tracing
