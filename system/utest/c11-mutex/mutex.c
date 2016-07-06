// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <runtime/mutex.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static mtx_t mutex;

static void xlog(const char* str) {
    uint64_t now = _magenta_current_time();
    unittest_printf("[%08llu.%08llu]: %s", now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300; times++) {
        mtx_lock(&mutex);
        _magenta_nanosleep(1000);
        mtx_unlock(&mutex);
    }

    xlog("thread 1 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150; times++) {
        mtx_lock(&mutex);
        _magenta_nanosleep(2000);
        mtx_unlock(&mutex);
    }

    xlog("thread 2 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100; times++) {
        mtx_lock(&mutex);
        _magenta_nanosleep(3000);
        mtx_unlock(&mutex);
    }

    xlog("thread 3 done\n");
    _magenta_thread_exit();
    return 0;
}

static bool got_lock_1 = false;
static bool got_lock_2 = false;
static bool got_lock_3 = false;

static int mutex_try_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300 || !got_lock_1; times++) {
        int status = mtx_trylock(&mutex);
        _magenta_nanosleep(1000);
        if (status == thrd_success) {
            got_lock_1 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 1 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_try_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150 || !got_lock_2; times++) {
        int status = mtx_trylock(&mutex);
        _magenta_nanosleep(2000);
        if (status == thrd_success) {
            got_lock_2 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 2 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_try_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100 || !got_lock_3; times++) {
        int status = mtx_trylock(&mutex);
        _magenta_nanosleep(3000);
        if (status == thrd_success) {
            got_lock_3 = true;
            mtx_unlock(&mutex);
        }
    }

    xlog("thread 3 done\n");
    _magenta_thread_exit();
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
    mx_handle_t handle1, handle2, handle3;

    handle1 = _magenta_thread_create(mutex_thread_1, NULL, "thread 1", 9);
    handle2 = _magenta_thread_create(mutex_thread_2, NULL, "thread 2", 9);
    handle3 = _magenta_thread_create(mutex_thread_3, NULL, "thread 3", 9);

    _magenta_handle_wait_one(handle1, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    _magenta_handle_wait_one(handle2, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    _magenta_handle_wait_one(handle3, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);

    _magenta_handle_close(handle1);
    _magenta_handle_close(handle2);
    _magenta_handle_close(handle3);
    END_TEST;
}

static bool test_try_mutexes(void) {
    BEGIN_TEST;
    mx_handle_t handle1, handle2, handle3;

    handle1 = _magenta_thread_create(mutex_try_thread_1, NULL, "thread 1", 9);
    handle2 = _magenta_thread_create(mutex_try_thread_2, NULL, "thread 2", 9);
    handle3 = _magenta_thread_create(mutex_try_thread_3, NULL, "thread 3", 9);

    _magenta_handle_wait_one(handle1, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    _magenta_handle_wait_one(handle2, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    _magenta_handle_wait_one(handle3, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);

    _magenta_handle_close(handle1);
    _magenta_handle_close(handle2);
    _magenta_handle_close(handle3);

    EXPECT_TRUE(got_lock_1, "failed to get lock 1");
    EXPECT_TRUE(got_lock_2, "failed to get lock 2");
    EXPECT_TRUE(got_lock_3, "failed to get lock 3");

    END_TEST;
}

static bool test_mtx_size(void) {
    // musl internally wires up mtx_t to be the same size as mxr_mutex_t.
    BEGIN_TEST;
    ASSERT_EQ(sizeof(mtx_t), sizeof(mxr_mutex_t), "mtx_t has an unexpected size!");
    END_TEST;
}

BEGIN_TEST_CASE(mtx_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_mutexes)
RUN_TEST(test_try_mutexes)
RUN_TEST(test_mtx_size)
END_TEST_CASE(mtx_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
