// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <perftest/perftest.h>

namespace {

// Performance test for clock_gettime()+CLOCK_MONOTONIC.  This is the
// main standard timer interface with nanosecond resolution on POSIX
// systems, including Linux.  This interface is worth testing because
// it is commonly used outside of Fuchsia.
bool ClockGettimeMonotonic() {
  timespec ts;
  ZX_ASSERT(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  perftest::DoNotOptimize(&ts);
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<ClockGettimeMonotonic>("ClockGettimeMonotonic");
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
