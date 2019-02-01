// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/event_spec.h"

#include <ostream>
#include <string>

namespace tracing {
namespace measure {

bool EventMatchesSpec(const trace::Record::Event& event,
                      const EventSpec& spec) {
  return event.name == spec.name && event.category == spec.category;
}

std::ostream& operator<<(std::ostream& os, EventSpec event_spec) {
  return os << event_spec.category.c_str() << ":" << event_spec.name.c_str();
}

}  // namespace measure
}  // namespace tracing
