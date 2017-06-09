// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <magenta/mx-syscall-numbers.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

extern mx_status_t bad_syscall(uint64_t num);

bool bad_access_test(void) {
    BEGIN_TEST;
    void* unmapped_addr = (void*)4096;
    mx_handle_t h[2];
    EXPECT_EQ(mx_channel_create(0, h, h + 1),
              0, "Error: channel create failed");
    EXPECT_LT(mx_channel_write(0, h[0], unmapped_addr, 1, NULL, 0),
              0, "Error: reading unmapped addr");
    EXPECT_LT(mx_channel_write(h[0], 0, (void*)KERNEL_ASPACE_BASE - 1, 5, NULL, 0),
              0, "Error: read crossing kernel boundary");
    EXPECT_LT(mx_channel_write(h[0], 0, (void*)KERNEL_ASPACE_BASE, 1, NULL, 0),
              0, "Error: read into kernel space");
    EXPECT_EQ(mx_channel_write(h[0], 0, (void*)&unmapped_addr, sizeof(void*), NULL, 0),
              0, "Good syscall failed");
    END_TEST;
}

bool bad_syscall_num_test(void) {
    BEGIN_TEST;
    EXPECT_EQ(bad_syscall(MX_SYS_COUNT), MX_ERR_BAD_SYSCALL, "");
    EXPECT_EQ(bad_syscall(0x80000000ull), MX_ERR_BAD_SYSCALL, "");
    EXPECT_EQ(bad_syscall(0xff00ff0000000000ull), MX_ERR_BAD_SYSCALL, "");
    EXPECT_EQ(bad_syscall(0xff00ff0000000010ull), MX_ERR_BAD_SYSCALL, "");
    END_TEST;
}

BEGIN_TEST_CASE(bad_syscall_tests)
RUN_TEST(bad_access_test)
RUN_TEST(bad_syscall_num_test)
END_TEST_CASE(bad_syscall_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
