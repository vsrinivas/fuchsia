// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <perftest/perftest.h>

namespace {

// Measure the time taken to malloc() and free() a |buffer_size|-byte block.
//
// This serves an example of a multi-step perf test.  It is also useful for
// getting a rough idea of the cost of malloc() and free().
bool MallocFreeTest(perftest::RepeatState* state, size_t buffer_size) {
  state->DeclareStep("malloc");
  state->DeclareStep("free");
  while (state->KeepRunning()) {
    void* block = malloc(buffer_size);
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

void RegisterTests() {
  perftest::RegisterTest("MallocFree/100bytes", MallocFreeTest, 100);
  perftest::RegisterTest("MallocFree/1024bytes", MallocFreeTest, 1024);
  perftest::RegisterTest("MallocFree/8192bytes", MallocFreeTest, 8192);
  perftest::RegisterTest("MallocFree/65536bytes", MallocFreeTest, 65536);
}

PERFTEST_CTOR(RegisterTests)

}  // namespace
