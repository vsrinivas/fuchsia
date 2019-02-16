// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../condition-generic/condition-generic.h"

#include <lib/sync/condition.h>

struct MutexWrapper {
    sync_mutex_t mtx;

    void lock() __TA_ACQUIRE(mtx) {
        sync_mutex_lock(&mtx);
    }

    void unlock() __TA_RELEASE(mtx) {
        sync_mutex_unlock(&mtx);
    }
};

struct ConditionWrapper {
    sync_condition_t condition;

    void signal() {
        sync_condition_signal(&condition);
    }

    void broadcast() {
        sync_condition_broadcast(&condition);
    }

    void wait(MutexWrapper* mtx) {
        sync_condition_wait(&condition, &mtx->mtx);
    }

    zx_status_t timedwait(MutexWrapper* mtx, zx_duration_t timeout) {
        return sync_condition_timedwait(&condition, &mtx->mtx, zx_deadline_after(timeout));
    }
};

using Condition = GenericConditionTest<MutexWrapper, ConditionWrapper>;

BEGIN_TEST_CASE(sync_condition_tests)
RUN_TEST(Condition::condition_test);
RUN_TEST(Condition::condition_timeout_test);
END_TEST_CASE(sync_condition_tests)
