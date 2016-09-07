// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "magma_util/platform/platform_device.h"
#include "mock/mock_mmio.h"
#include "gtest/gtest.h"

static void test_mmio(magma::PlatformMmio* mmio)
{
    ASSERT_NE(mmio, nullptr);

    // Verify we can write to and read from the mmio space.
    {
        uint32_t expected = 0xdeadbeef;
        mmio->Write32(expected, 0);
        uint32_t val = mmio->Read32(0);
        EXPECT_EQ(val, expected);

        mmio->Write32(expected, mmio->size() - sizeof(uint32_t));
        val = mmio->Read32(mmio->size() - sizeof(uint32_t));
        EXPECT_EQ(val, expected);
    }

    {
        uint64_t expected = 0xabcddeadbeef1234;
        mmio->Write64(expected, 0);
        uint64_t val = mmio->Read64(0);
        EXPECT_EQ(val, expected);

        mmio->Write64(expected, mmio->size() - sizeof(uint64_t));
        val = mmio->Read64(mmio->size() - sizeof(uint64_t));
        EXPECT_EQ(val, expected);
    }
}

TEST(MagmaUtil, MockMmio)
{
    test_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(8)).get());
    test_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(16)).get());
    test_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(64)).get());
    test_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(1024)).get());
}

TEST(MagmaUtil, PlatformMmio)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (!platform_device) {
        printf("No platform device\n");
        return;
    }

    uint32_t pci_bar = 0;

    // Map once
    auto mmio = platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_CACHED);
    EXPECT_NE(mmio, nullptr);

    // Map again same policy
    auto mmio2 = platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_CACHED);
    EXPECT_NE(mmio2, nullptr);

    // Map again different policy
    auto mmio3 =
        platform_device->CpuMapPciMmio(pci_bar, magma::PlatformMmio::CACHE_POLICY_UNCACHED);
    EXPECT_EQ(mmio3, nullptr);
}

TEST(MagmaUtil, PlatformMmioAccess)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (!platform_device) {
        printf("No platform device\n");
        return;
    }

    test_mmio(platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_CACHED).get());
    test_mmio(platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED).get());
    test_mmio(
        platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE).get());
    test_mmio(
        platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_WRITE_COMBINING).get());
}
