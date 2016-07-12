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
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>

extern int thread_entry(void* arg);

int print_fail(void) {
    EXPECT_TRUE(false, "Failed");
    mx_thread_exit();
    return 1; // Not reached
}

bool tis_test(void) {
    BEGIN_TEST;
    void* arg = (void*)0x1234567890abcdef;
    mx_handle_t handle = mx_thread_create(thread_entry, arg, "", 0);
    ASSERT_GE(handle, 0, "Error while thread creation");
    mx_status_t status = mx_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                  MX_TIME_INFINITE, NULL, NULL);
    ASSERT_GE(status, 0, "Error while thread wait");
    END_TEST;
}

BEGIN_TEST_CASE(tis_tests)
RUN_TEST(tis_test)
END_TEST_CASE(tis_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
