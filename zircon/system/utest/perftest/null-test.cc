// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>

namespace {

// This is a test that does nothing.  This is useful for measuring the
// overhead of the performance testing framework.  There will be some
// overhead in the perftest framework's loop that calls this function, and
// in the KeepRunning() calls that collect timing data.
bool NullTest() { return true; }

// This is a multi-step test where the steps do nothing.  This is useful
// for measuring the overhead of the performance testing framework.
bool Null5StepTest(perftest::RepeatState* state) {
  state->DeclareStep("step1");
  state->DeclareStep("step2");
  state->DeclareStep("step3");
  state->DeclareStep("step4");
  state->DeclareStep("step5");
  while (state->KeepRunning()) {
    state->NextStep();
    state->NextStep();
    state->NextStep();
    state->NextStep();
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<NullTest>("Null");
  perftest::RegisterTest("Null5Step", Null5StepTest);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
