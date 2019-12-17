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

  // We only check the count for Count events.
  if (lhs.type == CobaltEvent::Type::Count && lhs.count != rhs.count) {
    return false;
  }

  return true;
}

std::ostream& operator<<(std::ostream& os, const CobaltEvent& event) {
  return os << event.ToString();
}

std::string CobaltEvent::ToString() const {
  switch (type) {
    case Type::Occurrence:
      return fxl::StringPrintf("{type: occurrence, metric_id: %u, event_code: %u}", metric_id,
                               event_code);
    case Type::Count:
      return fxl::StringPrintf("{type: count, metric_id: %u, event_code: %u, count: %lu}",
                               metric_id, event_code, count);
  }
}

}  // namespace feedback
