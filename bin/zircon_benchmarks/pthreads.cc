// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include "lib/fxl/logging.h"
#include "test_runner.h"

namespace {

// A no-op helper entry-point for pthreads in this fixture.
void* ExitImmediately(void* arg) { return nullptr; }

// Benchmark for creating and joining on a pthread with a body that does
// nothing.
void PThreadCreateAndJoinTest() {
  pthread_t thread;
  FXL_CHECK(pthread_create(&thread, nullptr, ExitImmediately, nullptr) == 0);
  FXL_CHECK(pthread_join(thread, nullptr) == 0);
}

__attribute__((constructor)) void RegisterTests() {
  fbenchmark::RegisterTestFunc<PThreadCreateAndJoinTest>(
      "PThreadCreateAndJoinTest");
}

}  // namespace
