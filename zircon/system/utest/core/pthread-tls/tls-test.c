// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

static pthread_key_t tsd_key;
static pthread_key_t tsd_key_dtor;
static atomic_int dtor_count = ATOMIC_VAR_INIT(0);

void dtor(void* unused) {
    atomic_fetch_add(&dtor_count, 1);
}

static void test_tls(int thread_no) {
    int value1 = thread_no;
    int value2 = thread_no + 10;
    EXPECT_EQ(pthread_setspecific(tsd_key, &value1), 0,
              "Error while setting tls value");
    EXPECT_EQ(pthread_setspecific(tsd_key_dtor, &value2), 0,
              "Error while setting tls value");
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    int* v = pthread_getspecific(tsd_key);
    EXPECT_EQ(*v, value1, "wrong TLS value for key");
    v = pthread_getspecific(tsd_key_dtor);
    EXPECT_EQ(*v, value2, "wrong TLS value for key_dtor");
    unittest_printf("tls_test completed for thread: %d\n", thread_no);
}

static void* do_work(void* arg) {
    unittest_printf("do_work for thread: %d\n", *(int*)arg);
    test_tls(*(int*)arg);
    return NULL;
}

bool tls_test(void) {
    BEGIN_TEST;
    ASSERT_EQ(pthread_key_create(&tsd_key, NULL), 0, "Error during key creation");
    ASSERT_EQ(pthread_key_create(&tsd_key_dtor, dtor), 0, "Error during key creation");

    int expected_dtor_count = 0;

    // Run this 20 times for sanity check
    for (int i = 1; i <= 20; i++) {
        int main_thread = 1, thread_1 = i * 2, thread_2 = i * 2 + 1;

        pthread_t thread2, thread3;

        unittest_printf("creating thread: %d\n", thread_1);
        pthread_create(&thread2, NULL, do_work, &thread_1);

        unittest_printf("creating thread: %d\n", thread_2);
        pthread_create(&thread3, NULL, do_work, &thread_2);

        test_tls(main_thread);

        unittest_printf("joining thread: %d\n", thread_1);
        pthread_join(thread2, NULL);

        unittest_printf("joining thread: %d\n", thread_2);
        pthread_join(thread3, NULL);

        expected_dtor_count += 2;
        ASSERT_EQ(atomic_load(&dtor_count), expected_dtor_count, "dtors not run");
    }
    END_TEST;
}

BEGIN_TEST_CASE(tls_tests)
RUN_TEST(tls_test)
END_TEST_CASE(tls_tests)
