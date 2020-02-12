// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/syscalls.h>

#include <random>
#include <type_traits>

#include "perftest/perftest.h"

namespace {

// Measure the time per call, as well as throughput, for retrieving random bytes
// from |RandFunc|.
template <auto RandFunc>
bool GetFrom(perftest::RepeatState* state) {
  state->SetBytesProcessedPerRun(sizeof(RandFunc()));
  while (state->KeepRunning()) {
    auto r = RandFunc();
    perftest::DoNotOptimize(r);
  }
  return true;
}

// Measure the time per call, as well as throughput, for reading random bytes
// from std::radnom_device.
bool GetFromRandomDevice(perftest::RepeatState* state) {
  std::random_device rd;
  state->SetBytesProcessedPerRun(sizeof(decltype(rd)::result_type));
  while (state->KeepRunning()) {
    auto r = rd();
    perftest::DoNotOptimize(r);
  }
  return true;
}

// Measure the time per call, as well as throughput, for reading random data
// from Zircon.
bool GetFromZxCprng(perftest::RepeatState* state) {
  uint64_t r;
  state->SetBytesProcessedPerRun(sizeof(r));
  while (state->KeepRunning()) {
    zx_cprng_draw(&r, sizeof(r));
    perftest::DoNotOptimize(r);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Prng/LibCpp/RandomDevice", GetFromRandomDevice);
  perftest::RegisterTest("Prng/Zx/CprngDraw", GetFromZxCprng);

  // Deprecated/discouraged PRNGs.
  // rand() is not a cryptographically secure PRNG.
  perftest::RegisterTest("Prng/DoNotUse/LibC/Rand", GetFrom<rand>);
  // random() should never be used, as it simply generates sequential numbers.
  perftest::RegisterTest("Prng/DoNotUse/LibC/Random", GetFrom<random>);
}

PERFTEST_CTOR(RegisterTests);

}  // namespace
