// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_device.h"
#include "platform_thread.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, PlatformDevice)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(nullptr, platform_device);

    auto platform_mmio =
        platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    EXPECT_NE(nullptr, platform_mmio.get());
}

TEST(MagmaUtil, SchedulerProfile)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_NE(nullptr, platform_device);

    auto profile = platform_device->GetSchedulerProfile(magma::PlatformDevice::kPriorityHigher,
                                                        "msd/test-profile");
    EXPECT_NE(nullptr, profile);

    std::thread test_thread(
        [&profile]() { EXPECT_TRUE(magma::PlatformThreadHelper::SetProfile(profile.get())); });

    test_thread.join();
}
