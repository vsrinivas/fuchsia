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
#include <runtime/completion.h>
#include <runtime/status.h>
#include <runtime/thread.h>
#include <unittest/unittest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mxr_completion_t completion = MXR_COMPLETION_INIT;

#define ITERATIONS 64

static int completion_thread_wait(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        mx_status_t status = mxr_completion_wait(&completion, MX_TIME_INFINITE);
        ASSERT_EQ(status, NO_ERROR, "completion wait failed!");
    }

    return 0;
}

static int completion_thread_signal(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        mxr_completion_reset(&completion);
        mx_nanosleep(10000);
        mxr_completion_signal(&completion);
    }

    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;
    // Let's not accidentally break .bss'd completions
    static mxr_completion_t static_completion;
    mxr_completion_t completion = MXR_COMPLETION_INIT;
    int status = memcmp(&static_completion, &completion, sizeof(mxr_completion_t));
    EXPECT_EQ(status, 0, "mxr_completion's initializer is not all zeroes");
    END_TEST;
}

#define NUM_THREADS 16

static bool test_completions(void) {
    BEGIN_TEST;
    mxr_thread_t* signal_thread;
    mxr_thread_t* wait_thread[NUM_THREADS];

    for (int idx = 0; idx < NUM_THREADS; idx++)
        mxr_thread_create(completion_thread_wait, NULL, "completion wait", wait_thread + idx);
    mxr_thread_create(completion_thread_signal, NULL, "completion signal", &signal_thread);

    for (int idx = 0; idx < NUM_THREADS; idx++)
        mxr_thread_join(wait_thread[idx], NULL);
    mxr_thread_join(signal_thread, NULL);

    END_TEST;
}

static bool test_timeout(void) {
    BEGIN_TEST;
    mx_time_t timeout = 0u;
    mxr_completion_t completion = MXR_COMPLETION_INIT;
    for (int iteration = 0; iteration < 1000; iteration++) {
        timeout += 2000u;
        mx_status_t status = mxr_completion_wait(&completion, timeout);
        ASSERT_EQ(status, ERR_TIMED_OUT, "wait returned spuriously!");
    }
    END_TEST;
}

BEGIN_TEST_CASE(mxr_completion_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_completions)
RUN_TEST(test_timeout)
END_TEST_CASE(mxr_completion_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
