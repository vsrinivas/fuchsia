// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd.h"
#include "gtest/gtest.h"

TEST(MsdContext, CreateAndDestroy)
{

    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (!platform_device) {
        printf("No platform device\n");
        return;
    }

    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, platform_device->GetDeviceHandle());
    auto msd_connection = msd_device_open(msd_dev, 0);

    auto msd_context = msd_connection_create_context(msd_connection);
    EXPECT_NE(msd_context, nullptr);

    // just to catch crashes and such
    msd_context_destroy(msd_context);
}