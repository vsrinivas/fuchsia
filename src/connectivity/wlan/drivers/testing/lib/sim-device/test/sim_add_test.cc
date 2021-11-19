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

TEST_F(TestDeviceOps, DdkDeviceAdd) {
  device_add_args_t args = {
      .ctx = &dev_mgr_,
      .ops = &ops,
  };
  zx_device_t *device = nullptr;
  ASSERT_EQ(device_add(root_dev_, &args, &device), ZX_OK);
  ASSERT_NE(device, nullptr);
  ASSERT_TRUE(dev_mgr_.ContainsDevice(device));
}

TEST_F(TestDeviceOps, DdkDeviceAsyncRemove) {
  device_add_args_t args = {
      .ctx = &dev_mgr_,
      .ops = &ops,
  };
  zx_device_t *device = nullptr;
  ASSERT_EQ(device_add(root_dev_, &args, &device), ZX_OK);
  ASSERT_NE(device, nullptr);
  ASSERT_TRUE(dev_mgr_.ContainsDevice(device));

  device_async_remove(device);
  ASSERT_FALSE(dev_mgr_.ContainsDevice(device));
}

TEST_F(TestDeviceOps, DeviceInitTest) {
  device_add_args_t dev_args[NUM_DEVS] = {};
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
  device_add_args_t dev_args[NUM_DEVS] = {};
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
  device_add_args_t dev_args[kDeviceCnt] = {};
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
  device_add_args_t dev_args[kDeviceCnt] = {};
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
  device_add_args_t dev_args[NUM_DEVS] = {};
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

  for (const auto &dev : dev_mgr_) {
    // Check devices' parent other than root device
    if (dev->IsRootParent())
      continue;
    EXPECT_EQ(dev->Parent(), root_dev_);
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
  constexpr uint32_t kProtoId = ZX_PROTOCOL_WLANPHY;
  device_add_args_t add_args{
      .ops = nullptr,
      .proto_id = kProtoId,
  };
  zx_device_t *dev1 = nullptr;
  zx_device_t *dev2 = nullptr;
  auto status = dev_mgr_.DeviceAdd(root_dev_, &add_args, &dev1);
  ASSERT_EQ(status, ZX_OK);
  status = dev_mgr_.DeviceAdd(root_dev_, &add_args, &dev2);
  ASSERT_EQ(status, ZX_OK);

  // Retrieve via protocol:
  auto child_dev = dev_mgr_.FindFirstByProtocolId(kProtoId);
  ASSERT_EQ(child_dev, dev1);
  EXPECT_EQ(child_dev->Parent(), root_dev_);

  child_dev = dev_mgr_.FindLatestByProtocolId(kProtoId);
  ASSERT_EQ(child_dev, dev2);
  EXPECT_EQ(child_dev->Parent(), root_dev_);

  // Attempt to find a device with another protocol id which no devices have used. This should fail.
  child_dev = dev_mgr_.FindFirstByProtocolId(kProtoId + 1);
  ASSERT_EQ(child_dev, nullptr);
}

TEST_F(TestDeviceOps, ContainsDevice) {
  device_add_args_t add_args{
      .ops = nullptr,
      .proto_id = 42,
  };
  zx_device_t *dev = nullptr;
  auto status = dev_mgr_.DeviceAdd(root_dev_, &add_args, &dev);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(dev_mgr_.ContainsDevice(dev));
  // Create a pointer that can never be the same as dev no matter how lucky/unlucky we are.
  zx_device_t *invalid_ptr = reinterpret_cast<zx_device_t *>(~reinterpret_cast<uintptr_t>(dev));
  ASSERT_FALSE(dev_mgr_.ContainsDevice(invalid_ptr));
}

TEST_F(TestDeviceOps, RemoveParentBeforeChild) {
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

  // Removing the midlevel device will cause it and its child to be removed, but not the parent.
  dev_mgr_.DeviceAsyncRemove(level_two_dev);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(1));

  // Now removing the toplevel device should also succeed.
  dev_mgr_.DeviceAsyncRemove(level_one_dev);
  EXPECT_EQ(dev_mgr_.DeviceCount(), static_cast<size_t>(0));
}

// Used to verify that the address of the init operation remains the same
void EmptyInit(void *) {}

constexpr char kDeviceName[] = "test name";
constexpr zx_protocol_device_t kProtoOps = {.init = &EmptyInit};
constexpr device_power_state_info_t kPowerStates[] = {
    device_power_state_info_t{.state_id = DEV_POWER_STATE_D0},
    device_power_state_info_t{.state_id = DEV_POWER_STATE_D1},
};
constexpr device_performance_state_info_t kPerformanceStates[] = {
    device_performance_state_info_t{.state_id = DEV_PERFORMANCE_STATE_P0,
                                    .restore_latency = ZX_MSEC(3)},
    device_performance_state_info_t{.state_id = DEV_PERFORMANCE_STATE_P0,
                                    .restore_latency = ZX_MSEC(4)},
    device_performance_state_info_t{.state_id = DEV_PERFORMANCE_STATE_P0,
                                    .restore_latency = ZX_MSEC(5)},
};
const char *kFidlProtocolOffers[] = {
    "one",
    "two",
    "three",
    "four",
};
constexpr char kProxyArgs[] = "proxy args";

class DeviceAddArgsTest : public ::testing::Test {
 public:
 protected:
  static void CheckArgsEquality(const device_add_args_t &left, const device_add_args_t &right) {
    EXPECT_NE(left.name, right.name);
    EXPECT_STREQ(left.name, right.name);

    EXPECT_NE(left.ops, right.ops);
    EXPECT_EQ(left.ops->init, right.ops->init);

    EXPECT_NE(left.power_states, right.power_states);
    ASSERT_EQ(left.power_state_count, right.power_state_count);
    for (size_t i = 0; i < left.power_state_count; ++i) {
      EXPECT_EQ(left.power_states[i].state_id, right.power_states[i].state_id);
    }

    EXPECT_NE(left.performance_states, right.performance_states);
    ASSERT_EQ(left.performance_state_count, right.performance_state_count);
    for (size_t i = 0; i < left.performance_state_count; ++i) {
      EXPECT_EQ(left.performance_states[i].state_id, right.performance_states[i].state_id);
      EXPECT_EQ(left.performance_states[i].restore_latency,
                right.performance_states[i].restore_latency);
    }

    EXPECT_NE(left.fidl_protocol_offers, right.fidl_protocol_offers);
    ASSERT_EQ(left.fidl_protocol_offer_count, right.fidl_protocol_offer_count);
    for (size_t i = 0; i < left.fidl_protocol_offer_count; ++i) {
      EXPECT_NE(left.fidl_protocol_offers[i], right.fidl_protocol_offers[i]);
      EXPECT_STREQ(left.fidl_protocol_offers[i], right.fidl_protocol_offers[i]);
    }

    EXPECT_NE(left.proxy_args, right.proxy_args);
    EXPECT_STREQ(left.proxy_args, right.proxy_args);
  }

  const device_add_args_t original_args_ = {
      .name = kDeviceName,
      .ops = &kProtoOps,
      .power_states = kPowerStates,
      .power_state_count = std::size(kPowerStates),
      .performance_states = kPerformanceStates,
      .performance_state_count = std::size(kPerformanceStates),
      .fidl_protocol_offers = kFidlProtocolOffers,
      .fidl_protocol_offer_count = std::size(kFidlProtocolOffers),
      .proxy_args = kProxyArgs,
  };
};

TEST_F(DeviceAddArgsTest, ConstructFromPlainArgs) {
  DeviceAddArgs args(original_args_);

  CheckArgsEquality(args.Args(), original_args_);
}

TEST_F(DeviceAddArgsTest, AssignFromPlainArgs) {
  // Construct with some empty args, then assign.
  DeviceAddArgs args(device_add_args_t{});

  args = original_args_;

  CheckArgsEquality(args.Args(), original_args_);
}

TEST_F(DeviceAddArgsTest, ConstructFromDeviceAddArgs) {
  // Construct with some empty args, then assign.
  DeviceAddArgs source(original_args_);

  DeviceAddArgs destination(source);

  CheckArgsEquality(destination.Args(), original_args_);
}

TEST_F(DeviceAddArgsTest, AssignFromDeviceAddArgs) {
  // Construct with some empty args, then assign.
  DeviceAddArgs source(original_args_);

  DeviceAddArgs destination(device_add_args_t{});

  destination = source;

  CheckArgsEquality(destination.Args(), original_args_);
}

}  // namespace
}  // namespace simulation
}  // namespace wlan
