// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static mtx_t g_mutex = MTX_INIT;

static void xlog(const char* str) {
    uint64_t now = mx_time_get(MX_CLOCK_MONOTONIC);
    unittest_printf("[%08" PRIu64 ".%08" PRIu64 "]: %s",
                    now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300; times++) {
        mtx_lock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(1)));
        mtx_unlock(&g_mutex);
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150; times++) {
        mtx_lock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(2)));
        mtx_unlock(&g_mutex);
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100; times++) {
        mtx_lock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(3)));
        mtx_unlock(&g_mutex);
    }

    xlog("thread 3 done\n");
    return 0;
}

static bool got_lock_1 = false;
static bool got_lock_2 = false;
static bool got_lock_3 = false;

static int mutex_try_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300 || !got_lock_1; times++) {
        int status = mtx_trylock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(1)));
        if (status == thrd_success) {
            got_lock_1 = true;
            mtx_unlock(&g_mutex);
        }
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_try_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150 || !got_lock_2; times++) {
        int status = mtx_trylock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(2)));
        if (status == thrd_success) {
            got_lock_2 = true;
            mtx_unlock(&g_mutex);
        }
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_try_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100 || !got_lock_3; times++) {
        int status = mtx_trylock(&g_mutex);
        mx_nanosleep(mx_deadline_after(MX_USEC(3)));
        if (status == thrd_success) {
            got_lock_3 = true;
            mtx_unlock(&g_mutex);
        }
    }

    xlog("thread 3 done\n");
    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;

    int ret = mtx_init(&g_mutex, mtx_timed);
    ASSERT_EQ(ret, thrd_success, "failed to initialize mtx_t");

    END_TEST;
}

static bool test_mutexes(void) {
    BEGIN_TEST;
    thrd_t thread1, thread2, thread3;

    thrd_create_with_name(&thread1, mutex_thread_1, NULL, "thread 1");
    thrd_create_with_name(&thread2, mutex_thread_2, NULL, "thread 2");
    thrd_create_with_name(&thread3, mutex_thread_3, NULL, "thread 3");

    thrd_join(thread1, NULL);
    thrd_join(thread2, NULL);
    thrd_join(thread3, NULL);

    END_TEST;
}

static bool test_try_mutexes(void) {
    BEGIN_TEST;
    thrd_t thread1, thread2, thread3;

    thrd_create_with_name(&thread1, mutex_try_thread_1, NULL, "thread 1");
    thrd_create_with_name(&thread2, mutex_try_thread_2, NULL, "thread 2");
    thrd_create_with_name(&thread3, mutex_try_thread_3, NULL, "thread 3");

    thrd_join(thread1, NULL);
    thrd_join(thread2, NULL);
    thrd_join(thread3, NULL);

    EXPECT_TRUE(got_lock_1, "failed to get lock 1");
    EXPECT_TRUE(got_lock_2, "failed to get lock 2");
    EXPECT_TRUE(got_lock_3, "failed to get lock 3");

    END_TEST;
}

static bool test_static_initializer(void) {
    BEGIN_TEST;

    static mtx_t static_mutex = MTX_INIT;
    mtx_t auto_mutex;
    memset(&auto_mutex, 0xae, sizeof(auto_mutex));
    mtx_init(&auto_mutex, mtx_plain);

    EXPECT_BYTES_EQ((const uint8_t*)&static_mutex, (const uint8_t*)&auto_mutex, sizeof(mtx_t), "MTX_INIT and mtx_init differ!");

    END_TEST;
}

typedef struct {
    mtx_t mutex;
    mx_handle_t start_event;
    mx_handle_t done_event;
} timeout_args;

static int test_timeout_helper(void* ctx) {
    timeout_args* args = ctx;
    ASSERT_EQ(mtx_lock(&args->mutex), thrd_success, "f to lock");
    // Inform the main thread that we have acquired the lock.
    ASSERT_EQ(mx_object_signal(args->start_event, 0, MX_EVENT_SIGNALED), MX_OK,
              "failed to signal");
    // Wait until the main thread has completed its test.
    ASSERT_EQ(mx_object_wait_one(args->done_event, MX_EVENT_SIGNALED, MX_TIME_INFINITE, NULL),
              MX_OK, "failed to wait");
    ASSERT_EQ(mtx_unlock(&args->mutex), thrd_success, "failed to unlock");
    return 0;
}

static bool test_timeout_elapsed(void) {
    BEGIN_TEST;

    const mx_time_t kRelativeDeadline = MX_MSEC(100);
    // TODO(kulakowski) The kernel can currently return up to a
    // millisecond short in its internal conversion to lk_time_t. For
    // now, just accept this.
    const mx_time_t kAcceptableElapsedTime = MX_MSEC(99);

    timeout_args args;
    ASSERT_EQ(thrd_success, mtx_init(&args.mutex, mtx_plain), "could not create mutex");
    ASSERT_EQ(mx_event_create(0, &args.start_event), MX_OK, "could not create event");
    ASSERT_EQ(mx_event_create(0, &args.done_event), MX_OK, "could not create event");

    thrd_t helper;
    ASSERT_EQ(thrd_create(&helper, test_timeout_helper, &args), thrd_success, "");
    // Wait for the helper thread to acquire the lock.
    ASSERT_EQ(mx_object_wait_one(args.start_event, MX_EVENT_SIGNALED, MX_TIME_INFINITE, NULL),
              MX_OK, "failed to wait");

    for (int i = 0; i < 5; ++i) {
        mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
        struct timespec then = {
            .tv_sec = now / MX_SEC(1),
            .tv_nsec = now % MX_SEC(1),
        };
        then.tv_nsec += kRelativeDeadline;
        if (then.tv_nsec > (long)MX_SEC(1)) {
            then.tv_nsec -= MX_SEC(1);
            then.tv_sec += 1;
        }
        int rc = mtx_timedlock(&args.mutex, &then);
        ASSERT_EQ(rc, thrd_timedout, "wait should time out");
        mx_time_t elapsed = mx_time_get(MX_CLOCK_MONOTONIC) - now;
        if (elapsed < kAcceptableElapsedTime) {
            unittest_printf_critical("\nelapsed %" PRIu64
                            " < kAcceptableElapsedTime: %" PRIu64 "\n",
                            elapsed, kAcceptableElapsedTime);
            EXPECT_TRUE(false, "wait returned early");
        }
    }

    // Inform the helper thread that we are done.
    ASSERT_EQ(mx_object_signal(args.done_event, 0, MX_EVENT_SIGNALED),
              MX_OK, "failed to signal");
    ASSERT_EQ(thrd_join(helper, NULL), thrd_success, "failed to join");

    mtx_destroy(&args.mutex);
    ASSERT_EQ(mx_handle_close(args.start_event), MX_OK, "failed to close event");
    ASSERT_EQ(mx_handle_close(args.done_event), MX_OK, "failed to close event");

    END_TEST;
}

BEGIN_TEST_CASE(mtx_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_mutexes)
RUN_TEST(test_try_mutexes)
RUN_TEST(test_static_initializer)
RUN_TEST(test_timeout_elapsed)
END_TEST_CASE(mtx_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
