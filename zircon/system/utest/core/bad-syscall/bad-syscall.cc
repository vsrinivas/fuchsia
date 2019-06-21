// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <lib/zx/channel.h>
#include <zircon/zx-syscall-numbers.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

extern "C" zx_status_t bad_syscall(uint64_t num);

namespace {

const void* unmapped_addr = reinterpret_cast<void*>(4096);

TEST(BadAccessTest, InvalidMappedAddressFails) {
    zx::channel channel_a, channel_b;

    ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

    EXPECT_NOT_OK(channel_a.write(0, unmapped_addr, 1, nullptr, 0));
}

TEST(BadAccessTest, KernelMappedAddressChannelWriteFails) {
    zx::channel channel_a, channel_b;

    ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

    EXPECT_NOT_OK(channel_a.write(0, reinterpret_cast<void*>(KERNEL_ASPACE_BASE - 1),
                               5, nullptr, 0), "read crossing kernel boundary");
    EXPECT_NOT_OK(channel_a.write(0, reinterpret_cast<void*>(KERNEL_ASPACE_BASE),
                               1, nullptr, 0), "read into kernel space");
}

TEST(BadAccessTest, NormalMappedAddressChannelWriteSucceeds) {
    zx::channel channel_a, channel_b;

    ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

    EXPECT_OK(channel_a.write(0, reinterpret_cast<void*>(&unmapped_addr),
                              sizeof(void*), nullptr, 0), "Valid syscall failed");
}

TEST(BadAccessTest, SyscallNumTest) {
    ASSERT_DEATH(([](){ bad_syscall(ZX_SYS_COUNT);}));
    ASSERT_DEATH(([](){ bad_syscall(0x80000000ull);}));
    ASSERT_DEATH(([](){ bad_syscall(0xff00ff0000000000ull);}));
    ASSERT_DEATH(([](){ bad_syscall(0xff00ff0000000010ull);}));
}

} // namespace
