// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd_intel_device.h"
#include "msd_intel_driver.h"
#include "gtest/gtest.h"

class TestMsdIntelDevice {
public:
    static RegisterIo* register_io(MsdIntelDevice* device) { return device->register_io(); }
};

TEST(MsdIntelDevice, CreateAndDestroy)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (!platform_device) {
        printf("No platform device\n");
        return;
    }

    MsdIntelDriver* driver = MsdIntelDriver::Create();
    ASSERT_NE(driver, nullptr);

    std::unique_ptr<MsdIntelDevice> device(
        driver->CreateDevice(platform_device->GetDeviceHandle()));
    EXPECT_NE(device, nullptr);

    // test register read
    auto reg_io = TestMsdIntelDevice::register_io(device.get());
    ASSERT_NE(reg_io, nullptr);

    uint32_t value = reg_io->Read32(0x44038);
    EXPECT_EQ(0x1f2u, value);

    device.reset();

    MsdIntelDriver::Destroy(driver);
}
