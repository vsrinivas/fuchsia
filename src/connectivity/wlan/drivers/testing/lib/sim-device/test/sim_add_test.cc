// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <stdio.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "../device.h"

#define NUM_DEVS (10)

namespace wlan {
namespace simulation {
namespace {

class TestDeviceOps : public ::testing::Test {
 private:
  void SetUp() override {
    root_dev_ = dev_mgr_.GetRootDevice();
    init_count_ = 0;
    unbind_count_ = 0;

    ops.init = device_init;
    ops.unbind = device_unbind;
    ops.release = nullptr;
  }

 protected:
  static void device_init(void *ctx) {
    wlan::simulation::FakeDevMgr *dev_mgr = static_cast<wlan::simulation::FakeDevMgr *>(ctx);
    dev_mgr->DeviceInitReply(nullptr, ZX_OK, nullptr);
    init_count_++;
  }
  static void device_unbind(void *ctx) {
    wlan::simulation::FakeDevMgr *dev_mgr = static_cast<wlan::simulation::FakeDevMgr *>(ctx);
    dev_mgr->DeviceUnbindReply(nullptr);
    unbind_count_++;
  }
  static int init_count_;
  static int unbind_count_;
  wlan::simulation::FakeDevMgr dev_mgr_;
  zx_device_t *root_dev_;
  zx_protocol_device_t ops;
};

int TestDeviceOps::init_count_ = 0;
int TestDeviceOps::unbind_count_ = 0;

TEST_F(TestDeviceOps, DeviceInitTest) {
  device_add_args_t dev_args[NUM_DEVS];
  zx_device_t *ctx[NUM_DEVS];

  // Add NUM_DEVS devices and expect init_count_ to match.
  for (int i = 0; i < NUM_DEVS; i++) {
    dev_args[i].ops = &ops;
    dev_args[i].ctx = static_cast<void *>(&dev_mgr_);
    EXPECT_EQ(i, init_count_);
    EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(root_dev_, &dev_args[i], &ctx[i]));
  }
  EXPECT_EQ(NUM_DEVS, init_count_);
}

TEST_F(TestDeviceOps, DeviceUnbindTestFlat) {
  device_add_args_t dev_args[NUM_DEVS];
  zx_device_t *ctx[NUM_DEVS];

  for (int i = 0; i < NUM_DEVS; i++) {
    dev_args[i].ops = &ops;
    dev_args[i].ctx = static_cast<void *>(&dev_mgr_);
    EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(root_dev_, &dev_args[i], &ctx[i]));
  }

  // Remove NUM_DEVS leaf devices and expect unbind_count_ to match.
  for (int i = 0; i < NUM_DEVS; i++) {
    EXPECT_EQ(i, unbind_count_);
    dev_mgr_.DeviceAsyncRemove(ctx[i]);
  }
  EXPECT_EQ(NUM_DEVS, unbind_count_);

  // Remove non-existent device to ensure unbind_count_ does not increase.
  dev_mgr_.DeviceAsyncRemove(ctx[0]);
  EXPECT_EQ(NUM_DEVS, unbind_count_);
}

TEST_F(TestDeviceOps, DeviceUnbindHierarchyRootTest) {
  constexpr int kDeviceCnt = 5;
  device_add_args_t dev_args[kDeviceCnt];
  zx_device_t *ctx[kDeviceCnt];

  for (auto &dev_arg : dev_args) {
    dev_arg.ops = &ops;
    dev_arg.ctx = static_cast<void *>(&dev_mgr_);
  }

  // Create following tree.
  //     0
  //    / \
  //   1   2
  //  / \
  // 3   4
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(root_dev_, &dev_args[0], &ctx[0]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[0], &dev_args[1], &ctx[1]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[0], &dev_args[2], &ctx[2]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[1], &dev_args[3], &ctx[3]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[1], &dev_args[4], &ctx[4]));

  //  Remove root device and expect unbind to get invoked on all devices in hierarchy.
  dev_mgr_.DeviceAsyncRemove(ctx[0]);
  EXPECT_EQ(kDeviceCnt, unbind_count_);
}

TEST_F(TestDeviceOps, DeviceUnbindHierarchyMidTest) {
  constexpr int kDeviceCnt = 5;
  device_add_args_t dev_args[kDeviceCnt];
  zx_device_t *ctx[kDeviceCnt];

  for (auto &dev_arg : dev_args) {
    dev_arg.ops = &ops;
    dev_arg.ctx = static_cast<void *>(&dev_mgr_);
  }

  // Create following tree.
  //     0
  //    / \
  //   1   2
  //  / \
  // 3   4
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(root_dev_, &dev_args[0], &ctx[0]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[0], &dev_args[1], &ctx[1]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[0], &dev_args[2], &ctx[2]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[1], &dev_args[3], &ctx[3]));
  EXPECT_EQ(ZX_OK, dev_mgr_.DeviceAdd(ctx[1], &dev_args[4], &ctx[4]));

  // Remove a middle device and expect unbind to get invoked on just that sub tree.
  dev_mgr_.DeviceAsyncRemove(ctx[1]);
  EXPECT_EQ(3, unbind_count_);
}

TEST_F(TestDeviceOps, AddRemoveDevs) {
  device_add_args_t dev_args[NUM_DEVS];
  zx_device_t *ctx[NUM_DEVS];
  zx_status_t last_sts;

  printf("Adding a device\n");
  // Add the first device
  dev_args[0].name = "dev1";
  dev_args[0].ops = nullptr;
  last_sts = dev_mgr_.DeviceAdd(root_dev_, &dev_args[0], &ctx[0]);
  EXPECT_EQ(last_sts, ZX_OK);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(1));

  printf("Adding another device\n");
  // Add another device
  dev_args[1].name = "dev2";
  dev_args[1].ops = nullptr;
  last_sts = dev_mgr_.DeviceAdd(root_dev_, &dev_args[1], &ctx[1]);
  EXPECT_EQ(last_sts, ZX_OK);

  printf("Check if # devs is 2\n");
  // Check number of devices is TWO
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(2));

  printf("Iterate through the list\n");
  // Iterate through the dev list

  for (const auto &[dev, dev_info] : dev_mgr_) {
    // Check devices' parent other than root device
    if (dev == dev_mgr_.fake_root_dev_id_)
      continue;
    EXPECT_EQ(dev_info.parent, root_dev_);
  }

  printf("Remove the devs\n");

  // Remove the devices (deliberately not in the add order)
  dev_mgr_.DeviceAsyncRemove(ctx[1]);
  dev_mgr_.DeviceAsyncRemove(ctx[0]);

  printf("Check if # devs is 0\n");
  // Check if num devices in the list is zero
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(0));

  printf("Remove a non-existent dev\n");
  // Negative test...attempt to remove from empty list
  dev_mgr_.DeviceAsyncRemove(ctx[0]);
}

TEST_F(TestDeviceOps, RetrieveDevice) {
  wlan::simulation::FakeDevMgr dev_mgr_;

  device_add_args_t add_args{
      .ops = nullptr,
      .proto_id = 42,
  };
  zx_device_t *dev = nullptr;
  auto status = dev_mgr_.DeviceAdd(root_dev_, &add_args, &dev);
  ASSERT_EQ(status, ZX_OK);

  // Retrieve via protocol:
  auto child_dev = dev_mgr_.FindFirstByProtocolId(42);
  ASSERT_TRUE(child_dev.has_value());
  EXPECT_EQ(child_dev->parent, root_dev_);
  child_dev = dev_mgr_.FindFirstByProtocolId(43);
  ASSERT_FALSE(child_dev.has_value());

  // Retrieve via zx_device:
  child_dev = dev_mgr_.GetDevice(dev);
  ASSERT_TRUE(child_dev.has_value());
  EXPECT_EQ(child_dev->parent, root_dev_);
  child_dev = dev_mgr_.GetDevice(DeviceId(999).as_device());
  ASSERT_FALSE(child_dev.has_value());
  // This is the parent device of fake root device, it doesn't have any device info.
  child_dev = dev_mgr_.GetDevice(nullptr);
  ASSERT_FALSE(child_dev.has_value());
}

TEST_F(TestDeviceOps, RemoveParentBeforeChild) {
  wlan::simulation::FakeDevMgr dev_mgr_;

  // Use three level devices to verify the recursive removal not only works for directly parent
  // device.
  device_add_args_t add_args = {
      .ops = nullptr,
      .proto_id = 42,
  };

  zx_device_t *level_one_dev = nullptr;
  auto status = dev_mgr_.DeviceAdd(root_dev_, &add_args, &level_one_dev);
  ASSERT_EQ(status, ZX_OK);

  zx_device_t *level_two_dev = nullptr;
  status = dev_mgr_.DeviceAdd(level_one_dev, &add_args, &level_two_dev);
  ASSERT_EQ(status, ZX_OK);

  zx_device_t *level_three_dev = nullptr;
  status = dev_mgr_.DeviceAdd(level_two_dev, &add_args, &level_three_dev);
  ASSERT_EQ(status, ZX_OK);
  // Check number of devices is THREE
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(3));

  // We remove level one device first, the refcount will decreased, but since its child(level two
  // device) still existing, it will not be release immediately, thus the device count will not
  // change.
  dev_mgr_.DeviceAsyncRemove(level_one_dev);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(3));

  // then remove level two device , the refcount will decreased, but since its child(level three
  // device) still existing, it will not be release immediately, thus the device count will not
  // change.
  dev_mgr_.DeviceAsyncRemove(level_two_dev);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(3));

  // Once we remove the leaf device(level three device), the devices above in the tree will be
  // recursively release, device count will decrease to zero.
  dev_mgr_.DeviceAsyncRemove(level_three_dev);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(0));
}

}  // namespace
}  // namespace simulation
}  // namespace wlan
