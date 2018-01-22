// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include <zircon/syscalls.h>
#include <stdatomic.h>

static atomic_uint_fast64_t count = ATOMIC_VAR_INIT(0);

static int thread_func(void* arg) {
    uint64_t val = atomic_fetch_add(&count, 1);
    val++;
    if (val % 1000 == 0) {
        printf("Created %" PRId64 " threads, time %" PRId64 " us\n", val,
               zx_clock_get(ZX_CLOCK_MONOTONIC) / 1000000);
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
            printf("Joined %" PRId64 " threads, time %" PRId64 " us\n", val,
                   zx_clock_get(ZX_CLOCK_MONOTONIC) / 1000000);
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

    // TODO(phosek): The cast here shouldn't be required.
    printf("Created %" PRIdFAST64 " threads\n", (uint_fast64_t)atomic_load(&count));
    return 0;
}
