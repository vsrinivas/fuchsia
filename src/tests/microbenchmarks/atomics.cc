// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>

#include <perftest/perftest.h>

#include "src/lib/fxl/logging.h"

// Measure the times taken by atomic memory operations in the uncontended
// case (when no other threads are accessing the memory location).
//
// These atomics are important building blocks for other operations, such
// as mutexes, so it is useful to know their approximate costs.

namespace {

// Measure the time taken by an atomic increment.
template <typename Type>
bool TestAtomicIncrement(perftest::RepeatState* state) {
  std::atomic<Type> atomic_val(0);

  while (state->KeepRunning()) {
    ++atomic_val;
    // Prevent the compiler from optimizing away the atomic increment,
    // which it could potentially do because atomic_val is not otherwise
    // referenced.
    perftest::DoNotOptimize(&atomic_val);
  }
  return true;
}

// Measure the time taken by an atomic compare-and-swap.
template <typename Type>
bool TestAtomicCmpxchg(perftest::RepeatState* state) {
  std::atomic<Type> atomic_val(0);
  Type x = 0;

  while (state->KeepRunning()) {
    FXL_CHECK(atomic_val.compare_exchange_strong(x, x + 1));
    ++x;
    perftest::DoNotOptimize(&atomic_val);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Atomic/Increment/32bit", TestAtomicIncrement<uint32_t>);
  perftest::RegisterTest("Atomic/Increment/64bit", TestAtomicIncrement<uint64_t>);
  perftest::RegisterTest("Atomic/Cmpxchg/32bit", TestAtomicCmpxchg<uint32_t>);
  perftest::RegisterTest("Atomic/Cmpxchg/64bit", TestAtomicCmpxchg<uint64_t>);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
