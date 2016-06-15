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
#include <runtime/mutex.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mxr_mutex_t mutex = MXR_MUTEX_INIT;

static void xlog(const char* str) {
    uint64_t now = _magenta_current_time();
    printf("[%08llu.%08llu]: %s", now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
    xlog("thread 1 started\n");

    for (int times = 0; times < 300; times++) {
        mxr_mutex_lock(&mutex);
        _magenta_nanosleep(1000);
        mxr_mutex_unlock(&mutex);
    }

    xlog("thread 1 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_thread_2(void* arg) {
    xlog("thread 2 started\n");

    for (int times = 0; times < 150; times++) {
        mxr_mutex_lock(&mutex);
        _magenta_nanosleep(2000);
        mxr_mutex_unlock(&mutex);
    }

    xlog("thread 2 done\n");
    _magenta_thread_exit();
    return 0;
}

static int mutex_thread_3(void* arg) {
    xlog("thread 3 started\n");

    for (int times = 0; times < 100; times++) {
        mxr_mutex_lock(&mutex);
        _magenta_nanosleep(3000);
        mxr_mutex_unlock(&mutex);
    }

    xlog("thread 3 done\n");
    _magenta_thread_exit();
    return 0;
}

static void test_initializer(void) {
    // Let's not accidentally break .bss'd mutexes
    static mxr_mutex_t static_mutex;
    mxr_mutex_t mutex = MXR_MUTEX_INIT;
    if (memcmp(&static_mutex, &mutex, sizeof(mxr_mutex_t))) {
        printf("mxr_mutex's initializer is not all zeroes\n");
        exit(-1);
    }
}

static void test_mutexes(void) {
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
}

int main(int argc, char** argv) {
    test_initializer();
    test_mutexes();

    return 0;
}
