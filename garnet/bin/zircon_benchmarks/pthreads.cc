// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <perftest/perftest.h>

#include "src/lib/fxl/logging.h"

namespace {

// A no-op helper entry-point for pthreads in this fixture.
void* ExitImmediately(void* arg) { return nullptr; }

// Benchmark for creating and joining on a pthread with a body that does
// nothing.
bool PThreadCreateAndJoinTest() {
  pthread_t thread;
  FXL_CHECK(pthread_create(&thread, nullptr, ExitImmediately, nullptr) == 0);
  FXL_CHECK(pthread_join(thread, nullptr) == 0);
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<PThreadCreateAndJoinTest>("PThreadCreateAndJoinTest");
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
