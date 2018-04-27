// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/argument_value.h"

#include "garnet/public/lib/fxl/logging.h"

namespace tracing {
namespace measure {

MeasureArgumentValue::MeasureArgumentValue(std::vector<ArgumentValueSpec> specs)
    : specs_(std::move(specs)) {}

bool MeasureArgumentValue::RecordArgumentValue(
    const trace::Record::Event& event, const ArgumentValueSpec& spec) {
  for (const trace::Argument& argument : event.arguments) {
    if ((argument.name() == spec.argument_name) &&
        (argument.value().type() == trace::ArgumentType::kUint64)) {
      AddResult(spec.id, argument.value().GetUint64());
      return true;
    }
  }
  return false;
}

bool MeasureArgumentValue::Process(const trace::Record::Event& event) {
  for (const ArgumentValueSpec& spec : specs_) {
    if (EventMatchesSpec(event, spec.event) &&
        RecordArgumentValue(event, spec)) {
      return true;
    }
  }
  return false;
}

void MeasureArgumentValue::AddResult(uint64_t spec_id,
                                     uint64_t argument_value) {
  results_[spec_id].push_back(argument_value);
}

}  // namespace measure
}  // namespace tracing
