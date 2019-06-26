// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <sched.h>
#include <threads.h>

#include <zxtest/zxtest.h>

typedef struct cond_thread_args {
    mtx_t mutex;
    cnd_t cond;
    int threads_woken;
    int threads_started;
    int threads_woke_first_barrier;
} cond_thread_args_t;

static int cond_thread(void* arg) {
    cond_thread_args_t* args = (cond_thread_args_t*)(arg);

    mtx_lock(&args->mutex);
    args->threads_started++;
    cnd_wait(&args->cond, &args->mutex);
    args->threads_woke_first_barrier++;
    cnd_wait(&args->cond, &args->mutex);
    args->threads_woken++;
    mtx_unlock(&args->mutex);
    return 0;
}

TEST(ConditionalVariableTest, BroadcastSignalWait) {
    cond_thread_args_t args = {};

    ASSERT_EQ(mtx_init(&args.mutex, mtx_plain), thrd_success);
    ASSERT_EQ(cnd_init(&args.cond), thrd_success);

    thrd_t thread1, thread2, thread3;

    ASSERT_EQ(thrd_create(&thread1, cond_thread, &args), thrd_success);
    ASSERT_EQ(thrd_create(&thread2, cond_thread, &args), thrd_success);
    ASSERT_EQ(thrd_create(&thread3, cond_thread, &args), thrd_success);

    // Wait for all the threads to report that they've started.
    while (true) {
        mtx_lock(&args.mutex);
        int threads = args.threads_started;
        mtx_unlock(&args.mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    ASSERT_EQ(cnd_broadcast(&args.cond), thrd_success);

    // Wait for all the threads to report that they were woken.
    while (true) {
        mtx_lock(&args.mutex);
        int threads = args.threads_woke_first_barrier;
        mtx_unlock(&args.mutex);
        if (threads == 3) {
            break;
        }
        sched_yield();
    }

    for (int iteration = 0; iteration < 3; iteration++) {
        EXPECT_EQ(cnd_signal(&args.cond), thrd_success);

        // Wait for one thread to report that it was woken.
        while (true) {
            mtx_lock(&args.mutex);
            int threads = args.threads_woken;
            mtx_unlock(&args.mutex);
            if (threads == iteration + 1) {
                break;
            }
            sched_yield();
        }
    }

    EXPECT_EQ(thrd_join(thread1, NULL), thrd_success);
    EXPECT_EQ(thrd_join(thread2, NULL), thrd_success);
    EXPECT_EQ(thrd_join(thread3, NULL), thrd_success);
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

TEST(ConditionalVariableTest, ConditionalVariablesTimeout) {
    cnd_t cond = CND_INIT;
    mtx_t mutex = MTX_INIT;

    mtx_lock(&mutex);
    struct timespec delay;
    clock_gettime(CLOCK_REALTIME, &delay);
    time_add_nsec(&delay, 1000000);
    int result = cnd_timedwait(&cond, &mutex, &delay);
    mtx_unlock(&mutex);

    EXPECT_EQ(result, thrd_timedout, "Lock should have timedout");
}
