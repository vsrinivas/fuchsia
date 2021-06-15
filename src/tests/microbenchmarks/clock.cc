// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>
#include <zircon/syscalls.h>

namespace {

// Performance test for zx_clock_get_monotonic().  This is worth
// testing because it is a very commonly called syscall.  The kernel's
// implementation of the syscall is non-trivial and can be rather slow on
// some machines/VMs.
bool ClockGetMonotonicTest() {
  zx_clock_get_monotonic();
  return true;
}

bool TicksGetTest() {
  zx_ticks_get();
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<ClockGetMonotonicTest>("ClockGetMonotonic");
  perftest::RegisterSimpleTest<TicksGetTest>("TicksGet");
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
