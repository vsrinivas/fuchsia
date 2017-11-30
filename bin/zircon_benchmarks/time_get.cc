// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

#include "test_runner.h"

namespace {

// Benchmark for zx_time_get(ZX_CLOCK_MONOTONIC).  This is worth testing
// because it is a very commonly called syscall.  The kernel's
// implementation of the syscall is non-trivial and can be rather slow on
// some machines/VMs.
void TimeGetMonotonicTest() {
  zx_time_get(ZX_CLOCK_MONOTONIC);
}

__attribute__((constructor))
void RegisterTests() {
  fbenchmark::RegisterTestFunc<TimeGetMonotonicTest>("TimeGetMonotonic");
}

}  // namespace
