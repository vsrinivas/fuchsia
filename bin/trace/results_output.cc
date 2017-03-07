// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/results_output.h"

#include <algorithm>
#include <limits>
#include <numeric>

#include "apps/tracing/lib/trace/ticks.h"

namespace tracing {

namespace {

std::ostream& operator<<(std::ostream& os, measure::DurationSpec spec) {
  return os << "duration of " << spec.event.name << " (" << spec.event.category
            << ")";
}

std::ostream& operator<<(std::ostream& os, measure::TimeBetweenSpec spec) {
  return os << "time between " << spec.first_event.name << " ("
            << spec.first_event.category << ") and " << spec.second_event.name
            << " (" << spec.second_event.category;
}

double Average(const std::vector<Ticks>& samples) {
  uint64_t sum =
      std::accumulate(std::cbegin(samples), std::cend(samples), 0uLL);
  return static_cast<double>(sum) / samples.size();
}

double StdDev(const std::vector<Ticks>& samples, double average) {
  double sum_of_squared_deltas = 0.0;
  for (auto sample : samples) {
    sum_of_squared_deltas += (sample - average) * (sample - average);
  }
  return std::sqrt(sum_of_squared_deltas / samples.size());
}

template <typename Spec>
void OutputSingle(
    std::ostream& out,
    const Spec& spec,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& results,
    const Ticks ticks_per_second) {
  const double ticks_to_ms_scale = 1'000.0 / ticks_per_second;

  out << spec << " -> ";
  if (!results.count(spec.id) || results.at(spec.id).empty()) {
    out << " no results" << std::endl;
    return;
  }

  double average = Average(results.at(spec.id));
  double std_dev = StdDev(results.at(spec.id), average);

  out << "avg " << average * ticks_to_ms_scale << "ms out of "
      << results.at(spec.id).size() << " samples. "
      << "(std dev " << std_dev * ticks_to_ms_scale << ")" << std::endl;
}

}  // namespace

void OutputResults(
    std::ostream& out,
    const std::vector<measure::DurationSpec>& duration_specs,
    const std::vector<measure::TimeBetweenSpec>& time_between_specs,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& duration_results,
    const std::unordered_map<uint64_t, std::vector<Ticks>>&
        time_between_results) {
  out.precision(std::numeric_limits<double>::digits10);

  uint64_t ticks_per_second = GetTicksPerSecond();
  if (ticks_per_second == 0) {
    // Workaround for MG-593. This is the scale assumed in
    // chromium_exporter.h/cc too.
    FTL_LOG(WARNING)
        << "mx_ticks_per_second() returned 0, assuming 10^9 ticks per second.";
    ticks_per_second = 1'000'000'000;
  }

  for (auto& spec : duration_specs) {
    OutputSingle(out, spec, duration_results, ticks_per_second);
  }

  for (auto& spec : time_between_specs) {
    OutputSingle(out, spec, time_between_results, ticks_per_second);
  }
}

}  // namespace tracing
