// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/zx-syscall-numbers.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

extern "C" zx_status_t bad_syscall(uint64_t num);

namespace {

TEST(BadAccessTest, InvalidMapAddress) {
    void* unmapped_addr = (void*)4096;
    zx_handle_t h[2];
    EXPECT_EQ(zx_channel_create(0, h, h + 1),
              0, "Error: channel create failed");
    EXPECT_LT(zx_channel_write(0, h[0], unmapped_addr, 1, NULL, 0),
              0, "Error: reading unmapped addr");
    EXPECT_LT(zx_channel_write(h[0], 0, (void*)(KERNEL_ASPACE_BASE - 1), 5, NULL, 0),
              0, "Error: read crossing kernel boundary");
    EXPECT_LT(zx_channel_write(h[0], 0, (void*)KERNEL_ASPACE_BASE, 1, NULL, 0),
              0, "Error: read into kernel space");
    EXPECT_EQ(zx_channel_write(h[0], 0, (void*)&unmapped_addr, sizeof(void*), NULL, 0),
              0, "Good syscall failed");
}

TEST(BadAccessTest, SyscallNumTest) {
    ASSERT_DEATH(([](){ bad_syscall(ZX_SYS_COUNT);}));
    ASSERT_DEATH(([](){ bad_syscall(0x80000000ull);}));
    ASSERT_DEATH(([](){ bad_syscall(0xff00ff0000000000ull);}));
    ASSERT_DEATH(([](){ bad_syscall(0xff00ff0000000010ull);}));
}

} // namespace
