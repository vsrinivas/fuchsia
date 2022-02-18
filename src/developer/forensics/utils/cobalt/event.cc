// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/cobalt/event.h"

#include <algorithm>
#include <sstream>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace cobalt {
namespace {

std::string ToString(std::vector<uint32_t> dimensions) {
  std::stringstream output;
  output << "{";
  for (uint32_t idx = 0; idx < dimensions.size(); idx++) {
    output << dimensions[idx];

    if (idx < dimensions.size() - 1) {
      output << ", ";
    }
  }
  output << "}";
  return output.str();
}

}  // namespace

bool operator==(const Event& lhs, const Event& rhs) {
  if (lhs.type != rhs.type || lhs.metric_id != rhs.metric_id) {
    return false;
  }

  if (lhs.count != rhs.count) {
    return false;
  }

  if (lhs.dimensions.size() != rhs.dimensions.size()) {
    return false;
  } else if (lhs.dimensions.size() == 0) {
    return true;
  }

  std::vector lhs_events = lhs.dimensions;
  std::vector rhs_events = rhs.dimensions;
  std::sort(lhs_events.begin(), lhs_events.end());
  std::sort(rhs_events.begin(), rhs_events.end());
  return lhs_events == rhs_events;
}

std::ostream& operator<<(std::ostream& os, const Event& event) { return os << event.ToString(); }

std::string Event::ToString() const {
  switch (type) {
    case EventType::kOccurrence:
      return fxl::StringPrintf("{type: occurrence, metric_id: %u, dimensions: %s, count: %lu}",
                               metric_id, forensics::cobalt::ToString(dimensions).c_str(), count);
    case EventType::kInteger:
      return fxl::StringPrintf("{type: integer, metric_id: %u, dimensions: %s, count: %lu}",
                               metric_id, forensics::cobalt::ToString(dimensions).c_str(), count);
  }
}

}  // namespace cobalt
}  // namespace forensics
