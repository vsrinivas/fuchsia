// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_TRACER_H_
#define SRC_UI_LIB_ESCHER_UTIL_TRACER_H_

#include <vector>

#include "src/ui/lib/escher/util/stopwatch.h"

namespace escher {

// Trace support for Escher-on-Linux, used by the macros in trace_macros_impl.h.
// It can be instantiated on Fuchsia, but it won't be used.  Upon construction,
// registers itself as a global, which can be obtained via GetTracer().  Upon
// destruction, writes an "escher.trace" JSON file, in the format expected by
// chrome://tracing.
class Tracer {
 public:
  Tracer();
  ~Tracer();
  void AddTraceEvent(char phase, const char* category, const char* name);

  struct Event {
    char phase;
    const char* category;
    const char* name;
    uint64_t microseconds;
  };

 private:
  Stopwatch stopwatch_;
  std::vector<Event> events_;
};

Tracer* GetTracer();

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_TRACER_H_
