// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/cobalt/event.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace cobalt {

using fxl::StringPrintf;

bool operator==(const Event& lhs, const Event& rhs) {
  if (lhs.type != rhs.type) {
    return false;
  }

  if (lhs.metric_id != rhs.metric_id) {
    return false;
  }

  if (lhs.type != EventType::kMultidimensionalOccurrence &&
      lhs.dimensions[0] != rhs.dimensions[0]) {
    return false;
  }

  switch (lhs.type) {
    // If the event is an occurrence event then the count/usecs_elapsed field isn't relevant.
    case EventType::kOccurrence:
      return true;
    case EventType::kCount:
      return lhs.count == rhs.count;
    case EventType::kTimeElapsed:
      return lhs.usecs_elapsed == rhs.usecs_elapsed;
    case EventType::kMultidimensionalOccurrence:
      std::vector lhs_events = lhs.dimensions;
      std::vector rhs_events = rhs.dimensions;

      std::sort(lhs_events.begin(), lhs_events.end());
      std::sort(rhs_events.begin(), rhs_events.end());

      return lhs_events == rhs_events;
  }
}

std::ostream& operator<<(std::ostream& os, const Event& event) { return os << event.ToString(); }

std::string Event::ToString() const {
  switch (type) {
    case EventType::kOccurrence:
      return fxl::StringPrintf("{type: occurrence, metric_id: %u, dimension: %u}", metric_id,
                               dimensions[0]);
    case EventType::kCount:
      return fxl::StringPrintf("{type: count, metric_id: %u, dimension: %u, count: %lu}", metric_id,
                               dimensions[0], count);
    case EventType::kTimeElapsed:
      return fxl::StringPrintf(
          "{type: time elapsed, metric_id: %u, dimension: %u, usecs elapsed: %lu}", metric_id,
          dimensions[0], usecs_elapsed);
    case EventType::kMultidimensionalOccurrence:
      std::string dimensions_str;
      for (const auto& dimension : dimensions) {
        dimensions_str += fxl::StringPrintf("%u, ", dimension);
      }
      dimensions_str = dimensions_str.substr(0, dimensions_str.size() - 2);

      return fxl::StringPrintf(
          "{type: multi-dimensional occurrence, metric_id: %u, dimensions: [%s]}", metric_id,
          dimensions_str.c_str());
  }
}

}  // namespace cobalt
}  // namespace forensics
