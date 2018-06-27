// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <runtime/mutex.h>
#include <unittest/unittest.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static zxr_mutex_t mutex = ZXR_MUTEX_INIT;

static void xlog(const char* str) {
    uint64_t now = zx_clock_get_monotonic();
    unittest_printf("[%08" PRIu64 ".%08" PRIu64 "]: %s",
                    now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300; times++) {
        zxr_mutex_lock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        zxr_mutex_unlock(&mutex);
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150; times++) {
        zxr_mutex_lock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(2)));
        zxr_mutex_unlock(&mutex);
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100; times++) {
        zxr_mutex_lock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(3)));
        zxr_mutex_unlock(&mutex);
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
        zx_status_t status = zxr_mutex_trylock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        if (status == ZX_OK) {
            got_lock_1 = true;
            zxr_mutex_unlock(&mutex);
        }
    }

    xlog("thread 1 done\n");
    return 0;
}

static int mutex_try_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150 || !got_lock_2; times++) {
        zx_status_t status = zxr_mutex_trylock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(2)));
        if (status == ZX_OK) {
            got_lock_2 = true;
            zxr_mutex_unlock(&mutex);
        }
    }

    xlog("thread 2 done\n");
    return 0;
}

static int mutex_try_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100 || !got_lock_3; times++) {
        zx_status_t status = zxr_mutex_trylock(&mutex);
        zx_nanosleep(zx_deadline_after(ZX_USEC(3)));
        if (status == ZX_OK) {
            got_lock_3 = true;
            zxr_mutex_unlock(&mutex);
        }
    }

    xlog("thread 3 done\n");
    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;
    // Let's not accidentally break .bss'd mutexes
    static zxr_mutex_t static_mutex;
    zxr_mutex_t mutex = ZXR_MUTEX_INIT;
    int status = memcmp(&static_mutex, &mutex, sizeof(zxr_mutex_t));
    EXPECT_EQ(status, 0, "zxr_mutex's initializer is not all zeroes");
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


BEGIN_TEST_CASE(zxr_mutex_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_mutexes)
RUN_TEST(test_try_mutexes)
END_TEST_CASE(zxr_mutex_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
