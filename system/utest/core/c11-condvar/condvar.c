// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <sched.h>

#include <unittest/unittest.h>

static mtx_t mutex = MTX_INIT;
static cnd_t cond = CND_INIT;
static int threads_waked;
static int threads_started;
static int threads_woke_first_barrier;

static int cond_thread(void* arg) {
    mtx_lock(&mutex);
    threads_started++;
    cnd_wait(&cond, &mutex);
    threads_woke_first_barrier++;
    cnd_wait(&cond, &mutex);
    threads_waked++;
    mtx_unlock(&mutex);
    return 0;
}

bool cnd_test(void) {
    BEGIN_TEST;

    thrd_t thread1, thread2, thread3;

    thrd_create(&thread1, cond_thread, (void*)(uintptr_t)0);
    thrd_create(&thread2, cond_thread, (void*)(uintptr_t)1);
    thrd_create(&thread3, cond_thread, (void*)(uintptr_t)2);

    while (true) {
        mtx_lock(&mutex);
        int threads = threads_started;
        mtx_unlock(&mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    int result = cnd_broadcast(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to broadcast");

    while (true) {
        mtx_lock(&mutex);
        int threads = threads_woke_first_barrier;
        mtx_unlock(&mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while (true) {
        mtx_lock(&mutex);
        int threads = threads_waked;
        mtx_unlock(&mutex);
        if (threads == 1) {
            break;
        }
        sched_yield();
    }

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while (true) {
        mtx_lock(&mutex);
        int threads = threads_waked;
        mtx_unlock(&mutex);
        if (threads == 2) {
            break;
        }
        sched_yield();
    }

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while (true) {
        mtx_lock(&mutex);
        int threads = threads_waked;
        mtx_unlock(&mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    thrd_join(thread1, NULL);
    thrd_join(thread2, NULL);
    thrd_join(thread3, NULL);

    mtx_lock(&mutex);
    struct timespec delay;
    clock_gettime(CLOCK_REALTIME, &delay);
    delay.tv_sec += 2;
    result = cnd_timedwait(&cond, &mutex, &delay);
    mtx_unlock(&mutex);

    EXPECT_NEQ(result, thrd_success, "Lock should have timeout");

    END_TEST;
}

BEGIN_TEST_CASE(cnd_tests)
RUN_TEST(cnd_test)
END_TEST_CASE(cnd_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
