// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_device.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, PlatformDevice)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(nullptr, platform_device);

    auto platform_mmio =
        platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    EXPECT_NE(nullptr, platform_mmio.get());
}
