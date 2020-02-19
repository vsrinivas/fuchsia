// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <perftest/perftest.h>

namespace {

// Measure the times taken to lock and unlock a C11 mutex in the
// uncontended case.
bool MutexLockUnlockTest(perftest::RepeatState* state) {
  state->DeclareStep("lock");
  state->DeclareStep("unlock");
  mtx_t mutex;
  ZX_ASSERT(mtx_init(&mutex, mtx_plain) == thrd_success);
  while (state->KeepRunning()) {
    ZX_ASSERT(mtx_lock(&mutex) == thrd_success);
    state->NextStep();
    ZX_ASSERT(mtx_unlock(&mutex) == thrd_success);
  }
  mtx_destroy(&mutex);
  return true;
}

void RegisterTests() { perftest::RegisterTest("MutexLockUnlock", MutexLockUnlockTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
