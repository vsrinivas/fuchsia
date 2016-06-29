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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

static int thread_1(void* arg) {
    unittest_printf("thread 1 sleeping for .1 seconds\n");
    struct timespec t = (struct timespec){
        .tv_sec = 0,
        .tv_nsec = 100 * 1000 * 1000,
    };
    nanosleep(&t, NULL);

    unittest_printf("thread 1 calling _magenta_thread_exit()\n");
    _magenta_thread_exit();
    return 0;
}

bool threads_test(void) {
    BEGIN_TEST;
    mx_handle_t handle;
    unittest_printf("Welcome to thread test!\n");

    for (int i = 0; i != 4; ++i) {
        handle = _magenta_thread_create(thread_1, NULL, "thread 1", 9);
        ASSERT_GT(handle, 0, "Error while creating thread");
        unittest_printf("thread:%d created handle %d\n", i, handle);

        _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
        unittest_printf("thread:%d joined\n", i);

        _magenta_handle_close(handle);
    }

    unittest_printf("Attempting to create thread with a super long name. This should fail\n");
    handle = _magenta_thread_create(thread_1, NULL,
                                    "01234567890123456789012345678901234567890123456789012345678901234567890123456789", 81);
    ASSERT_LT(handle, 0, "Thread creation should have failed");
    unittest_printf("_magenta_thread_create returned %u\n", handle);

    unittest_printf("Attempting to create thread with a null. This should fail\n");
    handle = _magenta_thread_create(thread_1, NULL, NULL, 0);
    ASSERT_LT(handle, 0, "Error while creating thread");
    unittest_printf("_magenta_thread_create returned %u\n", handle);
    _magenta_handle_close(handle);

    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(threads_test)
END_TEST_CASE(threads_tests)

int main(void) {
    // TODO: remove this register once global constructors work
    unittest_register_test_case(&_threads_tests_element);
    return unittest_run_all_tests() ? 0 : -1;
}
