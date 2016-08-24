// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <sched.h>
#include <stdatomic.h>

#include <unittest/unittest.h>

static mtx_t mutex = MTX_INIT;
static cnd_t cond = CND_INIT;
static atomic_int process_waked;
static int threads_started;

static int cond_thread(void* arg) {
    mtx_lock(&mutex);
    threads_started++;
    cnd_wait(&cond, &mutex);
    cnd_wait(&cond, &mutex);
    atomic_fetch_add(&process_waked, 1);
    mtx_unlock(&mutex);
    return 0;
}

bool cnd_test(void) {
    BEGIN_TEST;

    thrd_t thread1, thread2, thread3;

    thrd_create(&thread1, cond_thread, (void*)(uintptr_t)0);
    thrd_create(&thread2, cond_thread, (void*)(uintptr_t)1);
    thrd_create(&thread3, cond_thread, (void*)(uintptr_t)2);

    int threads = 0;
    while (threads != 3) {
        sched_yield();
        mtx_lock(&mutex);
        threads = threads_started;
        mtx_unlock(&mutex);
    }

    int result = cnd_broadcast(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to broadcast");

    int current_count;

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while ((current_count = atomic_load(&process_waked)) != 1) {
        sched_yield();
    }

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while ((current_count = atomic_load(&process_waked)) != 2) {
        sched_yield();
    }

    result = cnd_signal(&cond);
    EXPECT_EQ(result, thrd_success, "Failed to signal");
    while ((current_count = atomic_load(&process_waked)) != 3) {
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
