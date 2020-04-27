// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/status.h>

#include <ddk/debug.h>
#include <gtest/gtest.h>

#include "../device.h"

#define NUM_DEVS (10)

namespace wlan {
namespace simulation {
namespace {

TEST(TestDeviceOps, AddRemoveDevs) {
  device_add_args_t dev_args[NUM_DEVS];
  zx_device_t *ctx[NUM_DEVS];
  zx_status_t last_sts;
  zx_device_t *my_dev = DeviceId(123).as_device();
  wlan::simulation::FakeDevMgr dev_mgr;

  printf("Adding a device\n");
  // Add the first device
  dev_args[0].name = "dev1";
  last_sts = dev_mgr.DeviceAdd(my_dev, &dev_args[0], &ctx[0]);
  EXPECT_EQ(last_sts, ZX_OK);
  EXPECT_EQ(dev_mgr.DevicesCount(), static_cast<size_t>(1));

  printf("Adding another device\n");
  // Add another device
  dev_args[1].name = "dev2";
  last_sts = dev_mgr.DeviceAdd(my_dev, &dev_args[1], &ctx[1]);
  EXPECT_EQ(last_sts, ZX_OK);

  printf("Check if # devs is 2\n");
  // Check number of devices is TWO
  EXPECT_EQ(dev_mgr.DevicesCount(), static_cast<size_t>(2));

  printf("Iterate through the list\n");
  // Iterate through the dev list

  for (const auto &[_dev, dev_info] : dev_mgr) {
    EXPECT_EQ(dev_info.parent, my_dev);
  }

  printf("Remove the devs\n");

  // Remove the devices (deliberately not in the add order)
  dev_mgr.DeviceAsyncRemove(ctx[1]);
  dev_mgr.DeviceAsyncRemove(ctx[0]);

  printf("Check if # devs is 0\n");
  // Check if num devices in the list is zero
  EXPECT_EQ(dev_mgr.DevicesCount(), static_cast<size_t>(0));

  printf("Remove a non-existent dev\n");
  // Negative test...attempt to remove from empty list
  dev_mgr.DeviceAsyncRemove(ctx[0]);
}

TEST(TestDeviceOps, RetrieveDevice) {
  zx_device_t *fakeParent = DeviceId(123).as_device();
  wlan::simulation::FakeDevMgr dev_mgr;

  device_add_args_t add_args{
      .proto_id = 42,
  };
  zx_device_t *dev = nullptr;
  auto status = dev_mgr.DeviceAdd(fakeParent, &add_args, &dev);
  ASSERT_EQ(status, ZX_OK);

  // Retrieve via protocol:
  auto child_dev = dev_mgr.FindFirstByProtocolId(42);
  ASSERT_TRUE(child_dev.has_value());
  EXPECT_EQ(child_dev->parent, fakeParent);
  child_dev = dev_mgr.FindFirstByProtocolId(43);
  ASSERT_FALSE(child_dev.has_value());

  // Retrieve via zx_device:
  child_dev = dev_mgr.GetDevice(dev);
  ASSERT_TRUE(child_dev.has_value());
  EXPECT_EQ(child_dev->parent, fakeParent);
  child_dev = dev_mgr.GetDevice(DeviceId(999).as_device());
  ASSERT_FALSE(child_dev.has_value());
  child_dev = dev_mgr.GetDevice(nullptr);
  ASSERT_FALSE(child_dev.has_value());
}

}  // namespace
}  // namespace simulation
}  // namespace wlan
