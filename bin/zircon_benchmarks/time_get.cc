// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

namespace {

// Benchmark for zx_time_get(ZX_CLOCK_MONOTONIC).  This is worth testing
// because it is a very commonly called syscall.  The kernel's
// implementation of the syscall is non-trivial and can be rather slow on
// some machines/VMs.
void TimeGetMonotonic(benchmark::State& state) {
  while (state.KeepRunning())
    zx_time_get(ZX_CLOCK_MONOTONIC);
}
BENCHMARK(TimeGetMonotonic);

}  // namespace
