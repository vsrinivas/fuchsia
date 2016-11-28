// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/ticks.h"

#include <magenta/syscalls.h>

namespace tracing {

Ticks GetTicksNow() {
  return mx_time_get(MX_CLOCK_MONOTONIC);
}

Ticks GetTicksPerSecond() {
  return 1000000000u;
}

}  // namespace tracing
