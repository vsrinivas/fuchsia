// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls-ddk.h>
#include <unittest/unittest.h>

int bad_syscall_test(void) {
    BEGIN_TEST;
    void* unmapped_addr = (void*)4096;
    EXPECT_LT(mx_debug_write(unmapped_addr, 1), 0, "Error: reading unmapped addr");
    EXPECT_LT(mx_debug_write((void*)KERNEL_ASPACE_BASE - 1, 5), 0, "Error: read crossing kernel boundary");
    EXPECT_LT(mx_debug_write((void*)KERNEL_ASPACE_BASE, 1), 0, "Error: read into kernel space");
    EXPECT_EQ(mx_debug_write((void*)&unmapped_addr, sizeof(void*)), (int)sizeof(void*),
              "Good syscall failed");
    END_TEST;
}

BEGIN_TEST_CASE(bad_syscall_tests)
RUN_TEST(bad_syscall_test)
END_TEST_CASE(bad_syscall_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif