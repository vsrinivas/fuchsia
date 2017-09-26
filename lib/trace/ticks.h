// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_TRACE_TICKS_H_
#define GARNET_LIB_TRACE_TICKS_H_

#include <stdint.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace tracing {

// Represents a moment in the trace timeline.
using Ticks = uint64_t;

#ifdef __Fuchsia__

// Gets the current timestamp in ticks elapsed since some arbitrary epoch.
inline Ticks GetTicksNow() {
  return zx_ticks_get();
}

// Gets the tick resolution in ticks per second.
inline Ticks GetTicksPerSecond() {
  return zx_ticks_per_second();
}

#endif  // __Fuchsia__

}  // namespace tracing

#endif  // GARNET_LIB_TRACE_TICKS_H_
