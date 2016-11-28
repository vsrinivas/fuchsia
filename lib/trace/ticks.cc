// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/ticks.h"

#include <magenta/prctl.h>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"

namespace tracing {

// TODO(jeffbrown): Define VDSO functions to provide architecture-neutral
// access to the tick counter.

#if defined(__x86_64__)

Ticks GetTicksNow() {
  uint32_t tsc_low;
  uint32_t tsc_hi;
  __asm__ __volatile__("rdtsc" : "=a"(tsc_low), "=d"(tsc_hi));
  return (static_cast<Ticks>(tsc_hi) << 32) | tsc_low;
}

Ticks GetTicksPerSecond() {
  uintptr_t ticks_per_ms = 0;
  mx_status_t status =
      mx_thread_arch_prctl(0, ARCH_GET_TSC_TICKS_PER_MS, &ticks_per_ms);
  FTL_CHECK(status == NO_ERROR);
  FTL_CHECK(ticks_per_ms != 0);
  return static_cast<Ticks>(ticks_per_ms) * 1000u;
}

#else

Ticks GetTicksNow() {
  return mx_time_get(MX_CLOCK_MONOTONIC);
}

Ticks GetTicksPerSecond() {
  return 1000000000u;
}

#endif

}  // namespace tracing
