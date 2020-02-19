// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <perftest/perftest.h>

namespace {

// Measure the time taken to malloc() and free() a 100-byte block.
//
// This serves an example of a multi-step perf test.  It is also useful for
// getting a rough idea of the cost of malloc() and free().
bool MallocFreeTest(perftest::RepeatState* state) {
  state->DeclareStep("malloc");
  state->DeclareStep("free");
  while (state->KeepRunning()) {
    void* block = malloc(100);
    // Clang can optimize away pairs of malloc() and free() calls;
    // prevent it from doing that.
    perftest::DoNotOptimize(block);
    if (!block) {
      return false;
    }
    state->NextStep();
    free(block);
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("MallocFree/100bytes", MallocFreeTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
