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

#include <assert.h>
#include <errno.h>
#include <magenta/syscalls.h>
#include <mxu/unittest.h>
#include <pthread.h>
#include <stdio.h>

static void* do_test(void* arg) {
    int thread_no = *(int*)arg;
    unittest_printf("do_test for thread: %d\n", thread_no);
    errno = -thread_no;
    _magenta_nanosleep(300 * 1000 * 1000);
    unittest_printf("comparing result for: %d\n", thread_no);
    EXPECT_EQ(errno, -thread_no, "Incorrect errno for this thread");
    return NULL;
}

bool errno_test(void) {
    BEGIN_TEST;
    int main_thread = 1, thread_1 = 2, thread_2 = 3;

    pthread_t thread2, thread3;

    unittest_printf("creating thread: %d\n", thread_1);
    pthread_create(&thread2, NULL, do_test, &thread_1);

    unittest_printf("creating thread: %d\n", thread_2);
    pthread_create(&thread3, NULL, do_test, &thread_2);

    do_test(&main_thread);

    unittest_printf("joining thread: %d\n", thread_1);
    pthread_join(thread2, NULL);

    unittest_printf("joining thread: %d\n", thread_2);
    pthread_join(thread3, NULL);

    END_TEST;
}

BEGIN_TEST_CASE(errno_tests)
RUN_TEST(errno_test)
END_TEST_CASE(errno_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
