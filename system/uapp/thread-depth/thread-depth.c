// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <runtime/thread.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>

static uint64_t count = 0;

static int thread_func(void* arg) {
    uint64_t val = atomic_fetch_add(&count, 1);
    val++;
    if (val % 1000 == 0) {
        printf("Created %lld threads, time %lld us\n", val, mx_current_time() / 1000000);
    }

    mxr_thread_t *thread = NULL;
    mx_status_t status = mxr_thread_create(thread_func, NULL, "depth", &thread);
    if (status == NO_ERROR) {
        status = mxr_thread_join(thread, NULL);
        if (status != NO_ERROR) {
            printf("Unexpected thread join return: %d\n", status);
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

    mxr_thread_t *thread = NULL;
    mx_status_t status = mxr_thread_create(thread_func, NULL, "depth", &thread);
    status = mxr_thread_join(thread, NULL);
    if (status != NO_ERROR) {
        printf("Unexpected thread join return: %d\n", status);
        return 1;
    }

    printf("Created %lld threads\n", count);
    return 0;
}
