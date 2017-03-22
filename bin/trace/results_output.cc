// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/results_output.h"

#include <algorithm>
#include <limits>
#include <numeric>

#include "apps/tracing/lib/measure/split.h"
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

void OutputSamples(std::ostream& out,
                   const std::vector<Ticks>& samples,
                   const double ticks_to_ms) {
  FTL_DCHECK(!samples.empty());
  if (samples.size() == 1) {
    out << samples[0] * ticks_to_ms << "ms";
    return;
  }

  double average = Average(samples);
  double std_dev = StdDev(samples, average);

  out << "avg " << average * ticks_to_ms << "ms out of " << samples.size()
      << " samples. "
      << "(std dev " << std_dev * ticks_to_ms << ")";
}

template <typename MeasureSpec>
void OutputMeasurement(
    std::ostream& out,
    const MeasureSpec& measure_spec,
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& samples,
    const double ticks_to_ms) {
  const uint64_t id = measure_spec.id;
  std::vector<measure::SampleRange> ranges;
  if (samples.count(id) > 0) {
    ranges = measure::Split(samples.at(id),
                            measurements.split_samples_at.count(id)
                                ? measurements.split_samples_at.at(id)
                                : std::vector<size_t>());
  }

  out << measure_spec << " -> ";
  if (ranges.empty()) {
    out << " no results" << std::endl;
    return;
  }

  if (ranges.size() == 1) {
    OutputSamples(out, ranges.front().samples, ticks_to_ms);
    out << std::endl;
    return;
  }

  out << std::endl;
  for (measure::SampleRange& range : ranges) {
    out << "  samples " << range.begin << " to " << range.end - 1 << ": ";
    OutputSamples(out, range.samples, ticks_to_ms);
    out << std::endl;
  }
}

}  // namespace

void OutputResults(
    std::ostream& out,
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<Ticks>>& samples) {
  out.precision(std::numeric_limits<double>::digits10);

  uint64_t ticks_per_second = GetTicksPerSecond();
  if (ticks_per_second == 0) {
    // Workaround for MG-593. This is the scale assumed in
    // chromium_exporter.h/cc too.
    FTL_LOG(WARNING)
        << "mx_ticks_per_second() returned 0, assuming 10^9 ticks per second.";
    ticks_per_second = 1'000'000'000;
  }
  const double ticks_to_ms = 1'000.0 / ticks_per_second;

  for (auto& measure_spec : measurements.duration) {
    OutputMeasurement(out, measure_spec, measurements, samples, ticks_to_ms);
  }
  for (auto& measure_spec : measurements.time_between) {
    OutputMeasurement(out, measure_spec, measurements, samples, ticks_to_ms);
  }
}

}  // namespace tracing
