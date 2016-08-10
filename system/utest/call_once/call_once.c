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

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
