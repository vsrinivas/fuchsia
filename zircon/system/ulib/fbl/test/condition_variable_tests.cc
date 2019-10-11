// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

namespace {

TEST(ConditionVariableTest, EmptySignal) {
  fbl::ConditionVariable cvar;
  cvar.Signal();
  cvar.Broadcast();
}

TEST(ConditionVariableTest, Wait) {
  struct State {
    fbl::Mutex mutex;
    fbl::ConditionVariable cvar;
  } state;

  thrd_t thread;
  fbl::AutoLock lock(&state.mutex);

  thrd_create(
      &thread,
      [](void* arg) {
        auto state = reinterpret_cast<State*>(arg);
        fbl::AutoLock lock(&state->mutex);
        state->cvar.Signal();
        return 0;
      },
      &state);

  state.cvar.Wait(&state.mutex);
  thrd_join(thread, nullptr);
}

}  // namespace
