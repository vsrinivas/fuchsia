// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_ARGUMENT_VALUE_H_
#define GARNET_LIB_MEASURE_ARGUMENT_VALUE_H_

#include <map>
#include <unordered_map>
#include <vector>

#include <trace-reader/reader.h>

#include "garnet/lib/measure/event_spec.h"
#include "lib/fxl/macros.h"

namespace tracing {
namespace measure {

// An "argument value" measurement records the value of the specified argument
// of a trace event. The argument must be of type uint64.
struct ArgumentValueSpec {
  uint64_t id;
  EventSpec event;

  std::string argument_name;
  std::string argument_unit;  // TODO(mariagl): use that for reporting.
};

class MeasureArgumentValue {
 public:
  explicit MeasureArgumentValue(std::vector<ArgumentValueSpec> specs);

  // Processes a recorded trace event. Returns true on success and false if the
  // record was ignored due to an error in the provided data.
  bool Process(const trace::Record::Event& event);

  // Returns the results of the measurements. The results are represented as a
  // map of measurement ids to lists of argument values of the matching trace
  // events.
  const std::unordered_map<uint64_t, std::vector<uint64_t>>& results() {
    return results_;
  }

 private:
  bool RecordArgumentValue(const trace::Record::Event& event,
                           const ArgumentValueSpec& spec);
  void AddResult(uint64_t spec_id, uint64_t argument_value);

  std::vector<ArgumentValueSpec> specs_;
  std::unordered_map<uint64_t, std::vector<uint64_t>> results_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MeasureArgumentValue);
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_ARGUMENT_VALUE_H_
