// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/results_output.h"

#include <algorithm>
#include <limits>
#include <numeric>

#include "garnet/public/lib/fxl/logging.h"

namespace tracing {

namespace {

double Average(const std::vector<double>& samples) {
  double sum = std::accumulate(std::cbegin(samples), std::cend(samples), 0.0);
  return static_cast<double>(sum) / samples.size();
}

double Min(const std::vector<double>& samples) {
  auto it = std::min_element(std::cbegin(samples), std::cend(samples));
  return *it;
}

double Max(const std::vector<double>& samples) {
  auto it = std::max_element(std::cbegin(samples), std::cend(samples));
  return *it;
}

double StdDev(const std::vector<double>& samples, double average) {
  double sum_of_squared_deltas = 0.0;
  for (auto sample : samples) {
    sum_of_squared_deltas += (sample - average) * (sample - average);
  }
  return std::sqrt(sum_of_squared_deltas / samples.size());
}

void OutputSamples(std::ostream& out, const std::vector<double>& values,
                   const std::string& unit) {
  FXL_DCHECK(!values.empty());
  if (values.size() == 1) {
    out << values.front() << unit;
    return;
  }

  double average = Average(values);
  double std_dev = StdDev(values, average);
  double min = Min(values);
  double max = Max(values);

  out << "avg " << average << unit << " out of " << values.size()
      << " samples. "
      << "(std dev " << std_dev << ", min " << min << ", max " << max << ")";
}

}  // namespace

void OutputResults(std::ostream& out,
                   const std::vector<measure::Result>& results) {
  out.precision(std::numeric_limits<double>::digits10);

  for (auto& result : results) {
    out << result.label << " -> ";
    if (result.samples.empty()) {
      out << " no results" << std::endl;
      continue;
    }

    if (result.samples.size() == 1) {
      OutputSamples(out, result.samples.front().values, result.unit);
      out << std::endl;
      continue;
    }

    out << std::endl;
    for (const measure::SampleGroup& sample_group : result.samples) {
      out << "  " << sample_group.label << ": ";
      OutputSamples(out, sample_group.values, result.unit);
      out << std::endl;
    }
  }
}

}  // namespace tracing
