// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/cobalt_event.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

using fxl::StringPrintf;

bool operator==(const CobaltEvent& lhs, const CobaltEvent& rhs) {
  if (lhs.type != rhs.type) {
    return false;
  }

  if (lhs.metric_id != rhs.metric_id) {
    return false;
  }

  if (lhs.event_code != rhs.event_code) {
    return false;
  }

  switch (lhs.type) {
    // If the event is an occurrence event then the count/usecs_elapsed field isn't relevant.
    case CobaltEventType::kOccurrence:
      return true;
    case CobaltEventType::kCount:
      return lhs.count == rhs.count;
    case CobaltEventType::kTimeElapsed:
      return lhs.usecs_elapsed == rhs.usecs_elapsed;
  }
}

std::ostream& operator<<(std::ostream& os, const CobaltEvent& event) {
  return os << event.ToString();
}

std::string CobaltEvent::ToString() const {
  switch (type) {
    case CobaltEventType::kOccurrence:
      return fxl::StringPrintf("{type: occurrence, metric_id: %u, event_code: %u}", metric_id,
                               event_code);
    case CobaltEventType::kCount:
      return fxl::StringPrintf("{type: count, metric_id: %u, event_code: %u, count: %lu}",
                               metric_id, event_code, count);
    case CobaltEventType::kTimeElapsed:
      return fxl::StringPrintf(
          "{type: time elapsed, metric_id: %u, event_code: %u, usecs elapsed: %lu}", metric_id,
          event_code, usecs_elapsed);
  }
}

}  // namespace feedback
