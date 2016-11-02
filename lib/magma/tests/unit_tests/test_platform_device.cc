// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "platform_device.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, PlatformDevice)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    uint16_t vendor_id = 0;
    bool ret = platform_device->ReadPciConfig16(0, &vendor_id);
    EXPECT_TRUE(ret);
    EXPECT_NE(vendor_id, 0);
}
