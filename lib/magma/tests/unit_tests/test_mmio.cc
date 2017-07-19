// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "mock/mock_mmio.h"
#include "platform_device.h"
#include "gtest/gtest.h"

static void test_mock_mmio(magma::PlatformMmio* mmio)
{
    ASSERT_NE(mmio, nullptr);

    // Verify we can write to and read from the mmio space.
    {
        uint32_t expected = 0xdeadbeef;
        mmio->Write32(0, expected);
        uint32_t val = mmio->Read32(0);
        EXPECT_EQ(val, expected);

        mmio->Write32(mmio->size() - sizeof(uint32_t), expected);
        val = mmio->Read32(mmio->size() - sizeof(uint32_t));
        EXPECT_EQ(val, expected);
    }

    {
        uint64_t expected = 0xabcddeadbeef1234;
        mmio->Write64(0, expected);
        uint64_t val = mmio->Read64(0);
        EXPECT_EQ(val, expected);

        mmio->Write64(mmio->size() - sizeof(uint64_t), expected);
        val = mmio->Read64(mmio->size() - sizeof(uint64_t));
        EXPECT_EQ(val, expected);
    }
}

TEST(MagmaUtil, MockMmio)
{
    test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(8)).get());
    test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(16)).get());
    test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(64)).get());
    test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(1024)).get());
}

TEST(MagmaUtil, PlatformMmio)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    uint32_t pci_bar = 0;

    // Map once
    auto mmio = platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_CACHED);
    EXPECT_NE(mmio, nullptr);

    // Map again same policy
    auto mmio2 = platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_CACHED);
    EXPECT_NE(mmio2, nullptr);

    // Map again different policy - this is now permitted though it's a bad idea.
    auto mmio3 =
        platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_UNCACHED);
    EXPECT_NE(mmio3, nullptr);
}
