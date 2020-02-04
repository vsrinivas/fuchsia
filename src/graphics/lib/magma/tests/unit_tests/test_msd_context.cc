// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "msd.h"

TEST(MsdContext, CreateAndDestroy) {
  auto msd_driver = msd_driver_create();
  ASSERT_NE(msd_driver, nullptr);

  auto msd_device = msd_driver_create_device(msd_driver, GetTestDeviceHandle());
  ASSERT_NE(msd_device, nullptr);

  auto msd_connection = msd_device_open(msd_device, 0);
  ASSERT_NE(msd_connection, nullptr);

  auto msd_context = msd_connection_create_context(msd_connection);
  EXPECT_NE(msd_context, nullptr);

  msd_context_destroy(msd_context);
  msd_connection_close(msd_connection);
  msd_device_destroy(msd_device);
  msd_driver_destroy(msd_driver);
}
