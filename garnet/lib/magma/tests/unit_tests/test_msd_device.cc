// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "msd.h"

TEST(MsdDevice, CreateAndDestroy) {
  msd_driver_t* driver = msd_driver_create();
  ASSERT_NE(driver, nullptr);

  msd_device_t* device = msd_driver_create_device(driver, nullptr);
  EXPECT_EQ(device, nullptr);

  device = msd_driver_create_device(driver, GetTestDeviceHandle());
  EXPECT_NE(device, nullptr);

  msd_device_destroy(device);

  msd_driver_destroy(driver);
}
