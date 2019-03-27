// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <unittest/unittest.h>

namespace fbl {
namespace {

bool EmptySignalTest() {
    BEGIN_TEST;

    ConditionVariable cvar;
    cvar.Signal();
    cvar.Broadcast();

    END_TEST;
}

bool WaitTest() {
    BEGIN_TEST;

    struct State {
        Mutex mutex;
        ConditionVariable cvar;
    } state;

    thrd_t thread;
    AutoLock lock(&state.mutex);

    thrd_create(&thread, [](void* arg) {
        auto state = reinterpret_cast<State*>(arg);
        AutoLock lock(&state->mutex);
        state->cvar.Signal();
        return 0;
    }, &state);

    state.cvar.Wait(&state.mutex);
    thrd_join(thread, NULL);

    END_TEST;
}

}  // namespace
}  // namespace fbl

BEGIN_TEST_CASE(ConditionVariableTests)
RUN_TEST(fbl::EmptySignalTest)
RUN_TEST(fbl::WaitTest)
END_TEST_CASE(ConditionVariableTests)
