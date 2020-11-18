// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_platform_device.h"

TEST(VsiPlatformDevice, ExternalSram) {
  auto device = MsdVsiPlatformDevice::Create(GetTestDeviceHandle());
  ASSERT_TRUE(device);
  std::optional<uint64_t> sram_base = device->GetExternalSramPhysicalBase();
  if (!sram_base.has_value()) {
    GTEST_SKIP();
  }
  EXPECT_EQ(0xFF000000u, sram_base.value());
}
