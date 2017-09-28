// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_EVENT_SPEC_H_
#define GARNET_LIB_MEASURE_EVENT_SPEC_H_

#include <string>

#include "zircon/system/ulib/trace-reader/include/trace-reader/reader.h"

namespace tracing {
namespace measure {

// Specifies a trace event.
struct EventSpec {
  fbl::String name;
  fbl::String category;
};

bool EventMatchesSpec(const trace::Record::Event& event,
                      const EventSpec& spec);

std::ostream& operator<<(std::ostream& os, EventSpec event_spec);

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_EVENT_SPEC_H_
