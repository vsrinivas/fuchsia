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

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

int bad_syscall_test(void) {
    BEGIN_TEST;
    void* unmapped_addr = (void*)4096;
    EXPECT_LT(_magenta_debug_write(unmapped_addr, 1), 0, "Error: reading unmapped addr");
    EXPECT_LT(_magenta_debug_write((void*)KERNEL_ASPACE_BASE - 1, 5), 0, "Error: read crossing kernel boundary");
    EXPECT_LT(_magenta_debug_write((void*)KERNEL_ASPACE_BASE, 1), 0, "Error: read into kernel space");
    EXPECT_EQ(_magenta_debug_write((void*)&unmapped_addr, sizeof(void*)), (int)sizeof(void*),
              "Good syscall failed");
    END_TEST;
}

BEGIN_TEST_CASE(bad_syscall_tests)
RUN_TEST(bad_syscall_test)
END_TEST_CASE(bad_syscall_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
