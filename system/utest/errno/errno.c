// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <pthread.h>
#include <stdio.h>

static void* do_test(void* arg) {
    int thread_no = *(int*)arg;
    unittest_printf("do_test for thread: %d\n", thread_no);
    errno = -thread_no;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(300)));
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

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
