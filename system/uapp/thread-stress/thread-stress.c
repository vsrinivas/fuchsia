// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>

#include <zircon/syscalls.h>

#define NUM_THREADS 1000

static int thread_func(void* arg) {
  return 0;
}

static void thread_create(thrd_t* thread) {
  int ret = thrd_create_with_name(thread, thread_func, NULL, "stress");
  if (ret != thrd_success) {
    printf("Failed to create thread: %d", ret);
  }
}

static void thread_join(thrd_t thread) {
  int ret = thrd_join(thread, NULL);
  if (ret != thrd_success) {
    printf("Failed to join thread: %d", ret);
  }
}

int main(int argc, char** argv) {
    printf("Running thread stress test...\n");
    thrd_t thread[NUM_THREADS];
    while (true) {
        zx_time_t start = zx_clock_get(ZX_CLOCK_MONOTONIC);
        for (int i = 0; i != NUM_THREADS; ++i) {
            thread_create(&thread[i]);
        }
        zx_time_t create = zx_clock_get(ZX_CLOCK_MONOTONIC);
        for (int i = 0; i != NUM_THREADS; ++i) {
            thread_join(thread[i]);
        }
        zx_time_t join = zx_clock_get(ZX_CLOCK_MONOTONIC);
        printf(
            "%d threads in %.2fs (create %.2fs, join %.2fs)\n",
            NUM_THREADS,
            (join - start) / 1e9,
            (create - start) / 1e9,
            (join - create) / 1e9);
    }
    return 0;
}
