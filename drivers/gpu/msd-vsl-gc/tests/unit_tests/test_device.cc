// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "helper/platform_device_helper.h"
#include "gtest/gtest.h"

// These tests are unit testing the functionality of MsdVslDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdVslDevice {
public:
    static void CreateAndDestroy()
    {
        std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
        EXPECT_NE(device, nullptr);
    }

    static void DeviceId()
    {
        std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
        ASSERT_NE(device, nullptr);
        EXPECT_EQ(0x7000u, device->device_id());
    }
};

TEST(MsdVslDevice, CreateAndDestroy) { TestMsdVslDevice::CreateAndDestroy(); }

TEST(MsdVslDevice, DeviceId) { TestMsdVslDevice::DeviceId(); }
