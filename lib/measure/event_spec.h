// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_MEASURE_EVENT_SPEC_H_
#define APPS_TRACING_LIB_MEASURE_EVENT_SPEC_H_

#include <string>

#include "garnet/lib/trace/reader.h"

namespace tracing {
namespace measure {

// Specifies a trace event.
struct EventSpec {
  std::string name;
  std::string category;
};

bool EventMatchesSpec(const reader::Record::Event& event,
                      const EventSpec& spec);

std::ostream& operator<<(std::ostream& os, EventSpec event_spec);

}  // namespace measure
}  // namespace tracing

#endif  // APPS_TRACING_LIB_MEASURE_EVENT_SPEC_H_
