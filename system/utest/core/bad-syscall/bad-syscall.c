// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
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

static void try_bad_syscall(void* arg) {
    uint64_t num = (uintptr_t)arg;
    mx_status_t status = bad_syscall(num);
    UNITTEST_TRACEF("bad syscall %#" PRIx64 " returned %d", num, status);
}

#define TRY_BAD_SYSCALL(num) \
    ASSERT_DEATH(try_bad_syscall, (void*)(uintptr_t)(num), \
                 "bad syscall did not crash")

bool bad_syscall_num_test(void) {
    BEGIN_TEST;
    TRY_BAD_SYSCALL(MX_SYS_COUNT);
    TRY_BAD_SYSCALL(0x80000000ull);
    TRY_BAD_SYSCALL(0xff00ff0000000000ull);
    TRY_BAD_SYSCALL(0xff00ff0000000010ull);
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
