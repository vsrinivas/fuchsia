// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sync/completion.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static completion_t completion = COMPLETION_INIT;

#define ITERATIONS 64

static int completion_thread_wait(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        mx_status_t status = completion_wait(&completion, MX_TIME_INFINITE);
        ASSERT_EQ(status, MX_OK, "completion wait failed!");
    }

    return 0;
}

static int completion_thread_signal(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        completion_reset(&completion);
        mx_nanosleep(mx_deadline_after(MX_USEC(10)));
        completion_signal(&completion);
    }

    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;
    // Let's not accidentally break .bss'd completions
    static completion_t static_completion;
    completion_t completion = COMPLETION_INIT;
    int status = memcmp(&static_completion, &completion, sizeof(completion_t));
    EXPECT_EQ(status, 0, "completion's initializer is not all zeroes");
    END_TEST;
}

#define NUM_THREADS 16

static bool test_completions(void) {
    BEGIN_TEST;
    thrd_t signal_thread;
    thrd_t wait_thread[NUM_THREADS];

    for (int idx = 0; idx < NUM_THREADS; idx++)
        thrd_create_with_name(wait_thread + idx, completion_thread_wait, NULL, "completion wait");
    thrd_create_with_name(&signal_thread, completion_thread_signal, NULL, "completion signal");

    for (int idx = 0; idx < NUM_THREADS; idx++)
        thrd_join(wait_thread[idx], NULL);
    thrd_join(signal_thread, NULL);

    END_TEST;
}

static bool test_timeout(void) {
    BEGIN_TEST;
    mx_time_t timeout = 0u;
    completion_t completion = COMPLETION_INIT;
    for (int iteration = 0; iteration < 1000; iteration++) {
        timeout += 2000u;
        mx_status_t status = completion_wait(&completion, timeout);
        ASSERT_EQ(status, MX_ERR_TIMED_OUT, "wait returned spuriously!");
    }
    END_TEST;
}

BEGIN_TEST_CASE(completion_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_completions)
RUN_TEST(test_timeout)
END_TEST_CASE(completion_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
