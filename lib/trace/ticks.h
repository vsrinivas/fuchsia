// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_TICKS_H_
#define APPS_TRACING_LIB_TRACE_TICKS_H_

#include <stdint.h>
#include <magenta/syscalls.h>

namespace tracing {

// Represents a moment in the trace timeline.
using Ticks = uint64_t;

// Gets the current timestamp in ticks elapsed since some arbitrary epoch.
inline Ticks GetTicksNow() {
  return mx_ticks_get();
}

// Gets the tick resolution in ticks per second.
inline Ticks GetTicksPerSecond() {
  return mx_ticks_per_second();
}

}  // namepsace tracing

#endif  // APPS_TRACING_LIB_TRACE_TICKS_H_
