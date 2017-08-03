// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unittest/unittest.h>

#define kNumThreads 16
#define kNumIterations 128

static pthread_barrier_t barrier;
static pthread_t threads[kNumThreads];
static int barriers_won[kNumThreads];

static bool barrier_wait_test(size_t idx) {
    BEGIN_HELPER;

    for (int iteration = 0u; iteration < kNumIterations; iteration++) {
        int result = pthread_barrier_wait(&barrier);
        if (result == PTHREAD_BARRIER_SERIAL_THREAD) {
            barriers_won[idx] += 1;
        } else {
            ASSERT_EQ(result, 0,
                      "Invalid return value from pthread_barrier_wait");
        }
    }

    END_HELPER;
}

static void* barrier_wait(void* arg) {
    // The real work is in the subroutine because functions using
    // ASSERT_* macros must return bool.
    (void)barrier_wait_test((uintptr_t)arg);
    return NULL;
}

static bool test_barrier(void) {
    BEGIN_TEST;

    ASSERT_EQ(pthread_barrier_init(&barrier, NULL, kNumThreads), 0, "Failed to initialize barrier!");

    for (int idx = 0; idx < kNumThreads; ++idx) {
        ASSERT_EQ(pthread_create(&threads[idx], NULL,
                                 &barrier_wait, (void*)(uintptr_t)idx), 0,
                  "Failed to create thread!");
    }

    for (int idx = 0; idx < kNumThreads; ++idx) {
        ASSERT_EQ(pthread_join(threads[idx], NULL), 0, "Failed to join thread!");
    }

    int total_barriers_won = 0;
    for (int idx = 0; idx < kNumThreads; ++idx) {
        total_barriers_won += barriers_won[idx];
    }
    ASSERT_EQ(total_barriers_won, kNumIterations, "Barrier busted!");

    END_TEST;
}

BEGIN_TEST_CASE(pthread_barrier_tests)
RUN_TEST(test_barrier)
END_TEST_CASE(pthread_barrier_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
