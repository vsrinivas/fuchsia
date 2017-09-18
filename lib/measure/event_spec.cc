// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/event_spec.h"

namespace tracing {
namespace measure {

bool EventMatchesSpec(const reader::Record::Event& event,
                      const EventSpec& spec) {
  return event.name == spec.name && event.category == spec.category;
}

std::ostream& operator<<(std::ostream& os, EventSpec event_spec) {
  return os << event_spec.category << ":" << event_spec.name;
}

}  // namespace measure
}  // namespace tracing
