// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>

namespace {

// Execute an empty loop for the given number of iterations.
bool NoOpLoop(perftest::RepeatState* state, int iteration_count) {
  while (state->KeepRunning()) {
    for (int i = 0; i < iteration_count; ++i) {
      // No-op that the compiler should not optimize away.
      __asm__ volatile("");
    }
  }
  return true;
}

void RegisterTests() {
  // This is intended as a simple way to test whether regression detection
  // is working: We can land a change that increases the iteration count
  // here and then manually check whether a regression gets reported, or
  // check whether the increase appears on the performance dashboard's
  // graph.
  perftest::RegisterTest("ExampleNoOpLoop", NoOpLoop, 1000);

  // Run these so we have reference values to compare against.
  perftest::RegisterTest("NoOpLoop/100", NoOpLoop, 100);
  perftest::RegisterTest("NoOpLoop/1000", NoOpLoop, 1000);
  perftest::RegisterTest("NoOpLoop/10000", NoOpLoop, 10000);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
