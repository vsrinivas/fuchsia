// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include <magenta/syscalls.h>
#include <stdatomic.h>

static uint64_t count = 0;

static int thread_func(void* arg) {
    uint64_t val = atomic_fetch_add(&count, 1);
    val++;
    if (val % 1000 == 0) {
        printf("Created %lld threads, time %lld us\n", val, mx_current_time() / 1000000);
    }

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, thread_func, NULL, "depth");
    if (ret == thrd_success) {
        ret = thrd_join(thread, NULL);
        if (ret != thrd_success) {
            printf("Unexpected thread join return: %d\n", ret);
            return 1;
        }
        val = atomic_fetch_sub(&count, 1);
        val--;
        if (val % 1000 == 0)
            printf("Joined %lld threads, time %lld us\n", val, mx_current_time() / 1000000);
    }

    return 0;
}

int main(int argc, char** argv) {
    printf("Running thread depth test...\n");

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, thread_func, NULL, "depth");
    ret = thrd_join(thread, NULL);
    if (ret != thrd_success) {
        printf("Unexpected thread join return: %d\n", ret);
        return 1;
    }

    printf("Created %lld threads\n", count);
    return 0;
}
