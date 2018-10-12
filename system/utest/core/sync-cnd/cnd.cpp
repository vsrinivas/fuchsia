// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../condvar-generic/condvar-generic.h"

#include <lib/sync/cnd.h>

struct MutexWrapper {
    sync_mtx_t mtx;

    void lock() __TA_ACQUIRE(mtx) {
        sync_mtx_lock(&mtx);
    }

    void unlock() __TA_RELEASE(mtx) {
        sync_mtx_unlock(&mtx);
    }
};

struct CndWrapper {
    sync_cnd_t cnd;

    void signal() {
        sync_cnd_signal(&cnd);
    }

    void broadcast() {
        sync_cnd_broadcast(&cnd);
    }

    void wait(MutexWrapper* mtx) {
        sync_cnd_wait(&cnd, &mtx->mtx);
    }

    zx_status_t timedwait(MutexWrapper* mtx, zx_duration_t timeout) {
        return sync_cnd_timedwait(&cnd, &mtx->mtx, zx_deadline_after(timeout));
    }
};

using Condvar = GenericCondvarTest<MutexWrapper, CndWrapper>;

BEGIN_TEST_CASE(sync_cnd_tests)
RUN_TEST(Condvar::cnd_test);
RUN_TEST(Condvar::cnd_timeout_test);
END_TEST_CASE(sync_cnd_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
