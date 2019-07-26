// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/status.h>
#include <ddk/debug.h>
#include "../device.h"
#include "gtest/gtest.h"

#define NUM_DEVS (10)

namespace {

class TestDeviceOps : public testing::Test {
 public:
  TestDeviceOps();
  ~TestDeviceOps();
  device_add_args_t dev_args[NUM_DEVS];
  zx_device_t *ctx[NUM_DEVS];
  uint32_t num_devices;
};

TestDeviceOps::TestDeviceOps() { num_devices = 0; }

TestDeviceOps::~TestDeviceOps() {}

TEST_F(TestDeviceOps, AddRemoveDevs) {
  uint32_t num_devs;
  zx_status_t last_sts;
  zx_device_t *my_dev, *dev, *parent;
  device_add_args_t *temp;

  printf("Allocate mem for parent dev context\n");
  // Allocate memory for parent
  my_dev = (zx_device_t *)std::calloc(1, 32);
  EXPECT_NE(my_dev, nullptr);

  printf("Adding a device\n");
  // Add the first device
  dev_args[0].name = "dev1";
  last_sts = wlan_sim_device_add(my_dev, &dev_args[0], &ctx[0]);
  EXPECT_EQ(last_sts, ZX_OK);
  num_devices++;

  printf("Adding another device\n");
  // Add another device
  dev_args[1].name = "dev2";
  last_sts = wlan_sim_device_add(my_dev, &dev_args[1], &ctx[1]);
  EXPECT_EQ(last_sts, ZX_OK);
  num_devices++;

  printf("Check if # devs is 2\n");
  // Check number of devices is TWO
  num_devs = wlan_sim_device_get_num_devices();
  EXPECT_EQ(num_devices, num_devs);

  printf("Iterate through the list\n");
  // Iterate through the dev list
  dev = wlan_sim_device_get_first(&parent, &temp);
  EXPECT_NE(dev, nullptr);
  EXPECT_EQ(parent, my_dev);
  while (dev != nullptr) {
    dev = wlan_sim_device_get_next(&parent, &temp);
    if (dev != nullptr) {
      EXPECT_EQ(parent, my_dev);
    }
  }

  printf("Remove the devs\n");
  // Remove the devices (deliberately not in the add order)

  last_sts = wlan_sim_device_remove(ctx[1]);
  EXPECT_EQ(last_sts, ZX_OK);

  last_sts = wlan_sim_device_remove(ctx[0]);
  EXPECT_EQ(last_sts, ZX_OK);

  printf("Check if # devs is 0\n");
  // Check if num devices in the list is zero
  num_devices = wlan_sim_device_get_num_devices();
  EXPECT_EQ(num_devices, 0u);

  printf("Remove a non-existent dev\n");
  // Negative test...attempt to remove from empty list
  last_sts = wlan_sim_device_remove(ctx[0]);
  EXPECT_NE(last_sts, ZX_OK);
  free(my_dev);
}

}  // namespace
