// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <gtest/gtest.h>
#include <helper/platform_device_helper.h>

#include "garnet/drivers/gpu/msd-qcom-adreno/src/msd_qcom_device.h"

TEST(Device, CreateAndDestroy) {
  auto device = MsdQcomDevice::Create(GetTestDeviceHandle());
  ASSERT_TRUE(device);
  DLOG("Got chip id: 0x%x", device->GetChipId());
}
