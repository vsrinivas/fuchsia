// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static mtx_t mutex = MTX_INIT;

static void xlog(const char* str) {
    uint64_t now = mx_current_time();
    unittest_printf("[%08llu.%08llu]: %s", now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300; times++) {
        mtx_lock(&mutex);
        mx_nanosleep(1000);
        mtx_unlock(&mutex);
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150; times++) {
        mtx_lock(&mutex);
        mx_nanosleep(2000);
        mtx_unlock(&mutex);
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100; times++) {
        mtx_lock(&mutex);
        mx_nanosleep(3000);
        mtx_unlock(&mutex);
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
        int status = mtx_trylock(&mutex);
        mx_nanosleep(1000);
        if (status == thrd_success) {
            got_lock_1 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_try_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150 || !got_lock_2; times++) {
        int status = mtx_trylock(&mutex);
        mx_nanosleep(2000);
        if (status == thrd_success) {
            got_lock_2 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_try_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100 || !got_lock_3; times++) {
        int status = mtx_trylock(&mutex);
        mx_nanosleep(3000);
        if (status == thrd_success) {
            got_lock_3 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 3 done\n");
    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;

    int ret = mtx_init(&mutex, mtx_timed);
    ASSERT_EQ(ret, thrd_success, "failed to initialize mtx_t");

    END_TEST;
}

static bool test_mutexes(void) {
    BEGIN_TEST;
    mxr_thread_t *handle1, *handle2, *handle3;

    mxr_thread_create(mutex_thread_1, NULL, "thread 1", &handle1);
    mxr_thread_create(mutex_thread_2, NULL, "thread 2", &handle2);
    mxr_thread_create(mutex_thread_3, NULL, "thread 3", &handle3);

    mxr_thread_join(handle1, NULL);
    mxr_thread_join(handle2, NULL);
    mxr_thread_join(handle3, NULL);

    END_TEST;
}

static bool test_try_mutexes(void) {
    BEGIN_TEST;
    mxr_thread_t *handle1, *handle2, *handle3;

    mxr_thread_create(mutex_try_thread_1, NULL, "thread 1", &handle1);
    mxr_thread_create(mutex_try_thread_2, NULL, "thread 2", &handle2);
    mxr_thread_create(mutex_try_thread_3, NULL, "thread 3", &handle3);

    mxr_thread_join(handle1, NULL);
    mxr_thread_join(handle2, NULL);
    mxr_thread_join(handle3, NULL);

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

BEGIN_TEST_CASE(mtx_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_mutexes)
RUN_TEST(test_try_mutexes)
RUN_TEST(test_static_initializer)
END_TEST_CASE(mtx_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
