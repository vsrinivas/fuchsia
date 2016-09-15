// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "forcewake.h"
#include "helper/platform_device_helper.h"
#include "magma_util/platform/platform_mmio.h"
#include "mock/mock_mmio.h"
#include "msd_intel_device.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestForceWake {
public:
    TestForceWake(registers::ForceWake::Domain domain) : domain_(domain)
    {
        switch (domain) {
        case registers::ForceWake::GEN8:
            offset_ = registers::ForceWake::kOffset;
            status_offset_ = registers::ForceWake::kStatusOffset;
            break;
        case registers::ForceWake::GEN9_RENDER:
            offset_ = registers::ForceWake::kRenderOffset;
            status_offset_ = registers::ForceWake::kRenderStatusOffset;
            break;
        }

        register_io_ =
            std::unique_ptr<RegisterIo>(new RegisterIo(MockMmio::Create(2 * 1024 * 1024)));
    }

    void Reset()
    {
        register_io_->mmio()->Write32(0, offset_);
        ForceWake::reset(register_io_.get(), domain_);
        EXPECT_EQ(0xFFFF0000, register_io_->mmio()->Read32(offset_));
    }

    void Request()
    {
        register_io_->mmio()->Write32(0, status_offset_);

        // Verify timeout waiting for status
        auto start = std::chrono::high_resolution_clock::now();
        ForceWake::request(register_io_.get(), domain_);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_EQ(0x00010001u, register_io_->mmio()->Read32(offset_));
        EXPECT_GE(elapsed.count(), (uint32_t)ForceWake::kRetryMaxMs);
    }

    void Release()
    {
        register_io_->mmio()->Write32(0xFFFFFFFF, status_offset_);

        // Verify timeout waiting for status
        auto start = std::chrono::high_resolution_clock::now();
        ForceWake::release(register_io_.get(), domain_);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_EQ(0x00010000u, register_io_->mmio()->Read32(offset_));
        EXPECT_GE(elapsed.count(), (uint32_t)ForceWake::kRetryMaxMs);
    }

private:
    std::unique_ptr<RegisterIo> register_io_;
    registers::ForceWake::Domain domain_;
    uint32_t offset_;
    uint32_t status_offset_;
};

TEST(ForceWake, Reset)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    uint16_t device_id;
    ASSERT_TRUE(platform_device->ReadPciConfig16(2, &device_id));

    if (MsdIntelDevice::is_gen8(device_id)) {
        TestForceWake test(registers::ForceWake::GEN8);
        test.Reset();
    }
    if (MsdIntelDevice::is_gen9(device_id)) {
        TestForceWake test(registers::ForceWake::GEN9_RENDER);
        test.Reset();
    }
}

TEST(ForceWake, Request)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    uint16_t device_id;
    ASSERT_TRUE(platform_device->ReadPciConfig16(2, &device_id));

    if (MsdIntelDevice::is_gen8(device_id)) {
        TestForceWake test(registers::ForceWake::GEN8);
        test.Request();
    }
    if (MsdIntelDevice::is_gen9(device_id)) {
        TestForceWake test(registers::ForceWake::GEN9_RENDER);
        test.Request();
    }
}

TEST(ForceWake, Release)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    uint16_t device_id;
    ASSERT_TRUE(platform_device->ReadPciConfig16(2, &device_id));

    if (MsdIntelDevice::is_gen8(device_id)) {
        TestForceWake test(registers::ForceWake::GEN8);
        test.Release();
    }
    if (MsdIntelDevice::is_gen9(device_id)) {
        TestForceWake test(registers::ForceWake::GEN9_RENDER);
        test.Release();
    }
}
