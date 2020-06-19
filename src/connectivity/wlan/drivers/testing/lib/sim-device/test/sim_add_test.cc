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
  wlan::simulation::FakeDevMgr dev_mgr;
  zx_device_t *my_dev = dev_mgr.GetRootDevice();

  printf("Adding a device\n");
  // Add the first device
  dev_args[0].name = "dev1";
  dev_args[0].ops = nullptr;
  last_sts = dev_mgr.DeviceAdd(my_dev, &dev_args[0], &ctx[0]);
  EXPECT_EQ(last_sts, ZX_OK);
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(1));

  printf("Adding another device\n");
  // Add another device
  dev_args[1].name = "dev2";
  dev_args[1].ops = nullptr;
  last_sts = dev_mgr.DeviceAdd(my_dev, &dev_args[1], &ctx[1]);
  EXPECT_EQ(last_sts, ZX_OK);

  printf("Check if # devs is 2\n");
  // Check number of devices is TWO
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(2));

  printf("Iterate through the list\n");
  // Iterate through the dev list

  for (const auto &[dev, dev_info] : dev_mgr) {
    // Check devices' parent other than root device
    if (dev == dev_mgr.fake_root_dev_id_)
      continue;
    EXPECT_EQ(dev_info.parent, my_dev);
  }

  printf("Remove the devs\n");

  // Remove the devices (deliberately not in the add order)
  dev_mgr.DeviceAsyncRemove(ctx[1]);
  dev_mgr.DeviceAsyncRemove(ctx[0]);

  printf("Check if # devs is 0\n");
  // Check if num devices in the list is zero
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(0));

  printf("Remove a non-existent dev\n");
  // Negative test...attempt to remove from empty list
  dev_mgr.DeviceAsyncRemove(ctx[0]);
}

TEST(TestDeviceOps, RetrieveDevice) {
  wlan::simulation::FakeDevMgr dev_mgr;
  zx_device_t *fakeParent = dev_mgr.GetRootDevice();

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
  // This is the parent device of fake root device, it doesn't have any device info.
  child_dev = dev_mgr.GetDevice(nullptr);
  ASSERT_FALSE(child_dev.has_value());
}

TEST(TestDeviceOps, RemoveParentBeforeChild) {
  wlan::simulation::FakeDevMgr dev_mgr;
  zx_device_t *fake_parent = dev_mgr.GetRootDevice();

  // Use three level devices to verify the recursive removal not only works for directly parent
  // device.
  device_add_args_t add_args = {
      .proto_id = 42,
  };

  zx_device_t *level_one_dev = nullptr;
  auto status = dev_mgr.DeviceAdd(fake_parent, &add_args, &level_one_dev);
  ASSERT_EQ(status, ZX_OK);

  zx_device_t *level_two_dev = nullptr;
  status = dev_mgr.DeviceAdd(level_one_dev, &add_args, &level_two_dev);
  ASSERT_EQ(status, ZX_OK);

  zx_device_t *level_three_dev = nullptr;
  status = dev_mgr.DeviceAdd(level_two_dev, &add_args, &level_three_dev);
  ASSERT_EQ(status, ZX_OK);
  // Check number of devices is THREE
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(3));

  // We remove level one device first, the refcount will decreased, but since its child(level two
  // device) still existing, it will not be release immediately, thus the device count will not
  // change.
  dev_mgr.DeviceAsyncRemove(level_one_dev);
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(3));

  // then remove level two device , the refcount will decreased, but since its child(level three
  // device) still existing, it will not be release immediately, thus the device count will not
  // change.
  dev_mgr.DeviceAsyncRemove(level_two_dev);
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(3));

  // Once we remove the leaf device(level three device), the devices above in the tree will be
  // recursively release, device count will decrease to zero.
  dev_mgr.DeviceAsyncRemove(level_three_dev);
  EXPECT_EQ(dev_mgr.DeviceCount(), static_cast<size_t>(0));
}

}  // namespace
}  // namespace simulation
}  // namespace wlan
