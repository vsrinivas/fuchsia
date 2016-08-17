// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/once.h>
#include <runtime/thread.h>
#include <stdatomic.h>

#include <unittest/unittest.h>

static atomic_int call_count;
static void counted_call(void) {
    atomic_fetch_add(&call_count, 1);
}

bool call_once_main_thread_test(void) {
    BEGIN_TEST;

    static mxr_once_t flag = MXR_ONCE_INIT;

    atomic_store(&call_count, 0);
    EXPECT_EQ(call_count, 0, "initial count nonzero");

    mxr_once(&flag, &counted_call);
    EXPECT_EQ(call_count, 1, "count not 1 after first call");

    mxr_once(&flag, &counted_call);
    EXPECT_EQ(call_count, 1, "count not 1 after second call");

    mxr_once(&flag, &counted_call);
    EXPECT_EQ(call_count, 1, "count not 1 after third call");

    END_TEST;
}

static int counted_call_thread(void* arg) {
    mxr_once(arg, &counted_call);
    return 0;
}

bool call_once_two_thread_test(void) {
    BEGIN_TEST;

    atomic_store(&call_count, 0);
    EXPECT_EQ(call_count, 0, "initial count nonzero");

    static mxr_once_t flag = MXR_ONCE_INIT;

    mxr_thread_t* thr;
    mx_status_t status = mxr_thread_create(&counted_call_thread, &flag,
                                           "second thread", &thr);
    EXPECT_EQ(status, 0, "mxr_thread_create");

    mxr_once(&flag, &counted_call);
    EXPECT_EQ(call_count, 1, "count not 1 after main thread's call");

    int thr_result;
    status = mxr_thread_join(thr, &thr_result);
    EXPECT_EQ(status, 0, "mxr_thread_join");
    EXPECT_EQ(thr_result, 0, "thread return value");

    EXPECT_EQ(call_count, 1, "count not 1 after join");

    END_TEST;
}

BEGIN_TEST_CASE(call_once_tests)
RUN_TEST(call_once_main_thread_test);
RUN_TEST(call_once_two_thread_test);
END_TEST_CASE(call_once_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
