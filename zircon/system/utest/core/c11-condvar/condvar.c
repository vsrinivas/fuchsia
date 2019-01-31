// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <sched.h>
#include <threads.h>

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

    threads_waked = 0;
    threads_started = 0;
    threads_woke_first_barrier = 0;

    thrd_t thread1, thread2, thread3;

    thrd_create(&thread1, cond_thread, (void*)(uintptr_t)0);
    thrd_create(&thread2, cond_thread, (void*)(uintptr_t)1);
    thrd_create(&thread3, cond_thread, (void*)(uintptr_t)2);

    // Wait for all the threads to report that they've started.
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

    // Wait for all the threads to report that they were woken.
    while (true) {
        mtx_lock(&mutex);
        int threads = threads_woke_first_barrier;
        mtx_unlock(&mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    for (int iteration = 0; iteration < 3; iteration++) {
        result = cnd_signal(&cond);
        EXPECT_EQ(result, thrd_success, "Failed to signal");

        // Wait for one thread to report that it was woken.
        while (true) {
            mtx_lock(&mutex);
            int threads = threads_waked;
            mtx_unlock(&mutex);
            if (threads == iteration + 1) {
                break;
            }
            sched_yield();
        }
    }

    thrd_join(thread1, NULL);
    thrd_join(thread2, NULL);
    thrd_join(thread3, NULL);

    END_TEST;
}

static void time_add_nsec(struct timespec* ts, int nsec) {
    const int kNsecPerSec = 1000000000;
    assert(nsec < kNsecPerSec);
    ts->tv_nsec += nsec;
    if (ts->tv_nsec > kNsecPerSec) {
        ts->tv_nsec -= kNsecPerSec;
        ts->tv_sec++;
    }
}

bool cnd_timedwait_timeout_test(void) {
    BEGIN_TEST;

    cnd_t cond = CND_INIT;
    mtx_t mutex = MTX_INIT;

    mtx_lock(&mutex);
    struct timespec delay;
    clock_gettime(CLOCK_REALTIME, &delay);
    time_add_nsec(&delay, 1000000);
    int result = cnd_timedwait(&cond, &mutex, &delay);
    mtx_unlock(&mutex);

    EXPECT_EQ(result, thrd_timedout, "Lock should have timeout");

    END_TEST;
}

BEGIN_TEST_CASE(cnd_tests)
RUN_TEST(cnd_test)
RUN_TEST(cnd_timedwait_timeout_test)
END_TEST_CASE(cnd_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
