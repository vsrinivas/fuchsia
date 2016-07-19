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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include <runtime/thread.h>
#include <runtime/tls.h>

volatile int threads_done[7];

static int thread_entry(void* arg) {
    int thread_number = (int)(intptr_t)arg;
    errno = thread_number;
    unittest_printf("thread %d sleeping for .1 seconds\n", thread_number);
    mx_nanosleep(100 * 1000 * 1000);
    EXPECT_EQ(errno, thread_number, "errno changed by someone!");
    threads_done[thread_number] = 1;
    return thread_number;
}

bool mxr_thread_test(void) {
    BEGIN_TEST;

    mxr_thread_t* thread;
    mx_status_t status;
    int return_value;

    unittest_printf("Welcome to thread test!\n");

    for (int i = 0; i != 4; ++i) {
        status = mxr_thread_create(thread_entry, (void*)(intptr_t)i, "mxr thread test", &thread);
        ASSERT_EQ(status, NO_ERROR, "Error while creating thread");

        status = mxr_thread_join(thread, &return_value);
        ASSERT_EQ(status, NO_ERROR, "Error while thread join");
        ASSERT_EQ(return_value, i, "Incorrect return from thread");
    }

    unittest_printf("Attempting to create thread with a super long name. This should fail\n");
    status = mxr_thread_create(thread_entry, NULL,
                               "01234567890123456789012345678901234567890123456789012345678901234567890123456789", &thread);
    ASSERT_NEQ(status, NO_ERROR, "thread creation should have thrown error");

    unittest_printf("Attempting to create thread with a null name. This should succeed\n");
    status = mxr_thread_create(thread_entry, (void*)(intptr_t)4, NULL, &thread);
    ASSERT_EQ(status, NO_ERROR, "Error returned from thread creation");

    status = mxr_thread_join(thread, &return_value);
    ASSERT_EQ(status, NO_ERROR, "Error while thread join");
    ASSERT_EQ(return_value, 4, "Incorrect return from thread");

    status = mxr_thread_create(thread_entry, (void*)(intptr_t)5, NULL, &thread);
    ASSERT_EQ(status, NO_ERROR, "Error returned from thread creation");
    status = mxr_thread_detach(thread);
    ASSERT_EQ(status, NO_ERROR, "Error while thread detach");

    while (!threads_done[5])
        mx_nanosleep(100 * 1000 * 1000);

    thread_entry((void*)(intptr_t)6);
    ASSERT_TRUE(threads_done[6], "All threads should have completed")

    END_TEST;
}

BEGIN_TEST_CASE(mxr_thread_tests)
RUN_TEST(mxr_thread_test)
END_TEST_CASE(mxr_thread_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
