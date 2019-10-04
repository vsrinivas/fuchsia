// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <helper/platform_device_helper.h>
#include <magma_util/register_io.h>

TEST(RegisterIo, Read) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  auto platform_mmio =
      platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
  ASSERT_TRUE(platform_mmio);

  auto register_io = std::make_unique<magma::RegisterIo>(std::move(platform_mmio));
  ASSERT_TRUE(register_io);

  DLOG("reading timestamp...");
  uint64_t timestamp = register_io->Read32(0x00000400 << 2);
  timestamp |= static_cast<uint64_t>(register_io->Read32(0x00000401 << 2)) << 32;
  EXPECT_NE(0u, timestamp);
  DLOG("read timestamp %lu", timestamp);

  DLOG("reading rbbm status...");
  uint32_t rbbm_status = register_io->Read32(0x00000210 << 2);
  DLOG("read rbbm_status 0x%x", rbbm_status);
  EXPECT_NE(0u, rbbm_status);
}

TEST(RegisterIo, Write) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  constexpr uint32_t kScratchRegisterOffset = 0x883 << 2;
  uint32_t original;

  {
    auto platform_mmio =
        platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    ASSERT_TRUE(platform_mmio);

    auto register_io = std::make_unique<magma::RegisterIo>(std::move(platform_mmio));

    original = register_io->Read32(kScratchRegisterOffset);
    register_io->Write32(kScratchRegisterOffset, ~original);
  }

  {
    auto platform_mmio =
        platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    ASSERT_TRUE(platform_mmio);

    auto register_io = std::make_unique<magma::RegisterIo>(std::move(platform_mmio));

    uint32_t value = register_io->Read32(kScratchRegisterOffset);
    EXPECT_EQ(value, ~original);

    register_io->Write32(kScratchRegisterOffset, original);
  }
}
