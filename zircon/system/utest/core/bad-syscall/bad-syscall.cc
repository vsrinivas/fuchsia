// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syscalls/zx-syscall-numbers.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <arch/kernel_aspace.h>
#include <zxtest/zxtest.h>

extern "C" zx_status_t bad_syscall(uint64_t num);
extern "C" zx_handle_t get_root_resource(void);

namespace {

void* unmapped_addr = reinterpret_cast<void*>(4096);

TEST(BadAccessTest, InvalidMappedAddressFails) {
  zx::channel channel_a, channel_b;

  ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

  EXPECT_NOT_OK(channel_a.write(0, unmapped_addr, 1, nullptr, 0));
}

TEST(BadAccessTest, KernelMappedAddressChannelWriteFails) {
  zx::channel channel_a, channel_b;

  ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

  EXPECT_NOT_OK(channel_a.write(0, reinterpret_cast<void*>(KERNEL_ASPACE_BASE - 1), 5, nullptr, 0),
                "read crossing kernel boundary");
  EXPECT_NOT_OK(channel_a.write(0, reinterpret_cast<void*>(KERNEL_ASPACE_BASE), 1, nullptr, 0),
                "read into kernel space");
}

TEST(BadAccessTest, NormalMappedAddressChannelWriteSucceeds) {
  zx::channel channel_a, channel_b;

  ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

  EXPECT_OK(channel_a.write(0, reinterpret_cast<void*>(&unmapped_addr), sizeof(void*), nullptr, 0),
            "Valid syscall failed");
}

TEST(BadAccessTest, SyscallNumTest) {
  ASSERT_DEATH(([]() { bad_syscall(ZX_SYS_COUNT); }));
  ASSERT_DEATH(([]() { bad_syscall(0x80000000ull); }));
  ASSERT_DEATH(([]() { bad_syscall(0xff00ff0000000000ull); }));
  ASSERT_DEATH(([]() { bad_syscall(0xff00ff0000000010ull); }));
}

#if defined(__x86_64__) && !defined(ENABLE_USER_PCI)
TEST(BadAccessTest, PciCfgPioRw) {
  EXPECT_EQ(zx_pci_cfg_pio_rw(get_root_resource(), 0, 0, 0, 0,
                              reinterpret_cast<uint32_t*>(unmapped_addr), 0, true),
            ZX_ERR_INVALID_ARGS);
}
#endif

TEST(BadAccessTest, ChannelReadHandle) {
  zx::channel channel_a, channel_b;

  ASSERT_OK(zx::channel::create(0, &channel_a, &channel_b));

  zx::process valid_handle;
  // Arbitrary valid handle to pass over the channel.
  ASSERT_OK(zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &valid_handle));

  zx_handle_t input_handles[] = {valid_handle.get()};
  ASSERT_OK(channel_a.write(/*flags=*/0, /*bytes=*/nullptr, /*num_bytes=*/0,
                            /*handles=*/input_handles,
                            /*num_handles=*/1));

  uint32_t actual_bytes, actual_handles;
  EXPECT_STATUS(channel_b.read(
                    /*flags=*/0, /*bytes=*/nullptr,
                    /*handles=*/reinterpret_cast<zx_handle_t*>(unmapped_addr),
                    /*num_bytes=*/0, /*num_handles=*/1, &actual_bytes, &actual_handles),
                ZX_ERR_INVALID_ARGS);
}

}  // namespace
