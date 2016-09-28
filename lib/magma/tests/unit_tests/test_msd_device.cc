// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd.h"
#include "gtest/gtest.h"

TEST(MsdDevice, CreateAndDestroy)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    msd_driver* driver = msd_driver_create();
    ASSERT_NE(driver, nullptr);

    msd_device* device = msd_driver_create_device(driver, nullptr);
    EXPECT_EQ(device, nullptr);

    device = msd_driver_create_device(driver, platform_device->GetDeviceHandle());
    EXPECT_NE(device, nullptr);

    msd_device_destroy(device);

    msd_driver_destroy(driver);
}
