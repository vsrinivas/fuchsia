// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/results.h"

#include "src/lib/fxl/logging.h"

#include <sstream>

namespace tracing {
namespace measure {

namespace {

std::string GetLabel(const measure::DurationSpec& spec) {
  std::ostringstream os;
  os << spec.event.name.c_str() << " (" << spec.event.category.c_str() << ")";
  return os.str();
}

std::string GetLabel(const measure::ArgumentValueSpec& spec) {
  std::ostringstream os;
  os << spec.event.name.c_str() << " (" << spec.event.category.c_str() << "), ";
  os << spec.argument_name.c_str();
  return os.str();
}

std::string GetLabel(const measure::TimeBetweenSpec& spec) {
  std::ostringstream os;
  os << spec.first_event.name.c_str() << " ("
     << spec.first_event.category.c_str() << ") to ";
  os << spec.second_event.name.c_str() << " ("
     << spec.second_event.category.c_str() << ")";
  return os.str();
}

std::string GetUnit(const measure::ArgumentValueSpec& spec) {
  return spec.argument_unit;
}

std::string GetUnit(const measure::DurationSpec& spec) { return "ms"; }

std::string GetUnit(const measure::TimeBetweenSpec& spec) { return "ms"; }

template <typename Spec, typename T>
Result ComputeSingle(Spec spec, const std::vector<T>& recorded_values) {
  Result result;
  if (spec.common.output_test_name.empty()) {
    result.label = GetLabel(spec);
  } else {
    result.label = spec.common.output_test_name;
  }
  result.unit = GetUnit(spec);
  result.split_first = spec.common.split_first;

  if ((spec.common.expected_sample_count > 0) &&
      (spec.common.expected_sample_count != recorded_values.size())) {
    FXL_LOG(ERROR) << "Number of recorded samples for an event " << result.label
                   << " does not match the expected number (expected "
                   << spec.common.expected_sample_count << ", got "
                   << recorded_values.size() << ").";
    return result;
  }

  std::copy(recorded_values.begin(), recorded_values.end(),
            std::back_inserter(result.values));
  return result;
}

template <typename T>
const T& get_or_default(const std::unordered_map<uint64_t, T>& dictionary,
                        uint64_t id, const T& default_value) {
  return dictionary.count(id) ? dictionary.at(id) : default_value;
}

std::vector<double> ticks_to_ms(const std::vector<uint64_t> ticks,
                                uint64_t ticks_per_second) {
  std::vector<double> milliseconds(ticks.size());
  double ms_per_tick = 1'000.0 / ticks_per_second;
  std::transform(ticks.begin(), ticks.end(), milliseconds.begin(),
                 [&ms_per_tick](uint64_t tick) { return tick * ms_per_tick; });
  return milliseconds;
}

}  // namespace

std::vector<Result> ComputeResults(
    const Measurements& measurements,
    const std::unordered_map<uint64_t, std::vector<uint64_t>>& recorded_values,
    uint64_t ticks_per_second) {
  std::vector<Result> results;
  const std::vector<uint64_t> no_recorded_values;
  std::string empty_string;

  for (auto& measure_spec : measurements.duration) {
    auto duration_values =
        ticks_to_ms(get_or_default(recorded_values, measure_spec.common.id,
                                   no_recorded_values),
                    ticks_per_second);
    results.push_back(ComputeSingle(measure_spec, duration_values));
  }
  for (auto& measure_spec : measurements.argument_value) {
    auto argument_values = get_or_default(
        recorded_values, measure_spec.common.id, no_recorded_values);
    results.push_back(ComputeSingle(measure_spec, argument_values));
  }
  for (auto& measure_spec : measurements.time_between) {
    auto time_between_values =
        ticks_to_ms(get_or_default(recorded_values, measure_spec.common.id,
                                   no_recorded_values),
                    ticks_per_second);
    results.push_back(ComputeSingle(measure_spec, time_between_values));
  }

  return results;
}

}  // namespace measure
}  // namespace tracing
