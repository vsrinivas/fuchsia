// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "device_controller_connection.h"
#include "driver_host.h"
#include "zx_device.h"

namespace {

namespace fdf = fuchsia_driver_framework;

using TestAddDeviceGroupCallback =
    fit::function<void(fuchsia_device_manager::wire::DeviceGroupDescriptor)>;

class FakeCoordinator : public fidl::WireServer<fuchsia_device_manager::Coordinator> {
 public:
  FakeCoordinator() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("driver_host-test-coordinator-loop");
  }
  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_device_manager::Coordinator> request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  void AddDevice(AddDeviceRequestView request, AddDeviceCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void ScheduleRemove(ScheduleRemoveRequestView request,
                      ScheduleRemoveCompleter::Sync& completer) override {}
  void AddCompositeDevice(AddCompositeDeviceRequestView request,
                          AddCompositeDeviceCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& completer) override {
    device_group_callback_(request->group_desc);
    completer.ReplySuccess();
  }
  void BindDevice(BindDeviceRequestView request, BindDeviceCompleter::Sync& completer) override {
    bind_count_++;
    completer.ReplyError(ZX_OK);
  }
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void LoadFirmware(LoadFirmwareRequestView request,
                    LoadFirmwareCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void GetMetadataSize(GetMetadataSizeRequestView request,
                       GetMetadataSizeCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void AddMetadata(AddMetadataRequestView request, AddMetadataCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void ScheduleUnbindChildren(ScheduleUnbindChildrenCompleter::Sync& completer) override {}

  uint32_t bind_count() { return bind_count_.load(); }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  void set_device_group_callback(TestAddDeviceGroupCallback callback) {
    device_group_callback_ = std::move(callback);
  }

 private:
  std::atomic<uint32_t> bind_count_ = 0;

  // The coordinator needs a separate loop so that when the DriverHost makes blocking calls into it,
  // it doesn't hang.
  async::Loop loop_;

  TestAddDeviceGroupCallback device_group_callback_;
};

class CoreTest : public zxtest::Test {
 protected:
  CoreTest() : ctx_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ctx_.loop().StartThread("driver_host-test-loop");
    internal::RegisterContextForApi(&ctx_);
    ASSERT_OK(zx_driver::Create("core-test", ctx_.inspect().drivers(), &drv_));

    auto driver = Driver::Create(drv_.get());
    ASSERT_OK(driver.status_value());
    driver_obj_ = *std::move(driver);
  }

  ~CoreTest() { internal::RegisterContextForApi(nullptr); }

  void Connect(fbl::RefPtr<zx_device> device) {
    auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
    ASSERT_OK(coordinator_endpoints.status_value());

    fidl::WireSharedClient client(std::move(coordinator_endpoints->client),
                                  ctx_.loop().dispatcher());

    auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
    ASSERT_OK(controller_endpoints.status_value());

    auto conn = DeviceControllerConnection::Create(&ctx_, device, std::move(client));

    DeviceControllerConnection::Bind(std::move(conn), std::move(controller_endpoints->server),
                                     ctx_.loop().dispatcher());

    ASSERT_OK(
        coordinator_.Connect(coordinator_.dispatcher(), std::move(coordinator_endpoints->server)));

    clients_.push_back(controller_endpoints->client.TakeChannel());
  }

  // This simulates receiving an unbind and remove request from the devcoordinator.
  void UnbindDevice(fbl::RefPtr<zx_device> dev) {
    fbl::AutoLock lock(&ctx_.api_lock());
    ctx_.DeviceUnbind(dev);
    // DeviceCompleteRemoval() will drop the device reference added by device_add().
    // Since we never called device_add(), we should increment the reference count here.
    fbl::RefPtr<zx_device_t> dev_add_ref(dev.get());
    __UNUSED auto ptr = fbl::ExportToRawPtr(&dev_add_ref);
    dev->removal_cb = [](zx_status_t) {};
    ctx_.DeviceCompleteRemoval(dev);
  }

  std::vector<zx::channel> clients_;
  DriverHostContext ctx_;
  fbl::RefPtr<zx_driver> drv_;
  fbl::RefPtr<Driver> driver_obj_;
  FakeCoordinator coordinator_;
};

TEST_F(CoreTest, LastDeviceUnbindStopsAsyncLoop) {
  EXPECT_EQ(0, driver_obj_->device_count());
  zx_protocol_device_t ops = {};
  {
    fbl::RefPtr<zx_device> dev;
    ASSERT_OK(zx_device::Create(&ctx_, "test", driver_obj_, &dev));

    EXPECT_EQ(1, driver_obj_->device_count());
    ASSERT_FALSE(driver_obj_->IsDispatcherShutdown());
    dev->set_ops(&ops);
    // Mark the device as "added" so that we try and call the release op on the device (and shut
    // down its dispatcher).
    dev->set_flag(DEV_FLAG_ADDED);

    ASSERT_NO_FATAL_FAILURE(Connect(dev));

    dev->unbind_cb = [](zx_status_t) {};
    UnbindDevice(dev);

    // Clean up the DeviceControllerConnection we set up in Connect().
    clients_.clear();
    ctx_.loop().Quit();
    ctx_.loop().JoinThreads();
    ASSERT_OK(ctx_.loop().ResetQuit());
    ASSERT_OK(ctx_.loop().RunUntilIdle());
    // Here the dev will go out of scope and fbl_recycle() will be called.
  }

  EXPECT_EQ(0, driver_obj_->device_count());

  ASSERT_TRUE(driver_obj_->IsDispatcherShutdown());
}

TEST_F(CoreTest, RebindNoChildren) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx_, "test", driver_obj_, &dev));

  zx_protocol_device_t ops = {};
  dev->set_ops(&ops);

  ASSERT_NO_FATAL_FAILURE(Connect(dev));

  EXPECT_EQ(device_rebind(dev.get()), ZX_OK);
  EXPECT_EQ(coordinator_.bind_count(), 1);

  // Join the thread running in the background, then run the rest of the tasks locally.
  ctx_.loop().Quit();
  ctx_.loop().JoinThreads();
  ctx_.loop().ResetQuit();
  ctx_.loop().RunUntilIdle();

  dev->set_flag(DEV_FLAG_DEAD);
  {
    fbl::AutoLock lock(&ctx_.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx_.DriverManagerRemove(std::move(dev));
  }
  ASSERT_OK(ctx_.loop().RunUntilIdle());
}

TEST_F(CoreTest, SystemPowerStateMapping) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx_, "test", driver_obj_, &dev));
  ASSERT_NO_FATAL_FAILURE(Connect(dev));

  ASSERT_OK(dev->SetPowerStates(internal::kDeviceDefaultPowerStates,
                                std::size(internal::kDeviceDefaultPowerStates)));

  // Use the default system power state mapping, but set `performance_state` values to be
  // incrementally increasing so the test can verify we select the correct one
  zx_device::SystemPowerStateMapping states_mapping(internal::kDeviceDefaultStateMapping);
  for (size_t i = 0; i < states_mapping.size(); i++) {
    states_mapping[i].performance_state = (uint32_t)i;
  }
  ASSERT_OK(dev->SetSystemPowerStateMapping(states_mapping));

  fuchsia_device::wire::SystemPowerStateInfo state_info;
  uint8_t suspend_reason = DEVICE_SUSPEND_REASON_SELECTIVE_SUSPEND;

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_REBOOT, &state_info,
                                                  &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_REBOOT);
  ASSERT_EQ(state_info.performance_state, 1);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER,
                                                  &state_info, &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_REBOOT_BOOTLOADER);
  ASSERT_EQ(state_info.performance_state, 2);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY, &state_info,
                                                  &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_REBOOT_RECOVERY);
  ASSERT_EQ(state_info.performance_state, 3);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_POWEROFF, &state_info,
                                                  &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_POWEROFF);
  ASSERT_EQ(state_info.performance_state, 4);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_MEXEC, &state_info,
                                                  &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_MEXEC);
  ASSERT_EQ(state_info.performance_state, 5);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_SUSPEND_RAM, &state_info,
                                                  &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_SUSPEND_RAM);
  ASSERT_EQ(state_info.performance_state, 6);

  ASSERT_OK(dev->get_dev_power_state_from_mapping(DEVICE_SUSPEND_FLAG_REBOOT_KERNEL_INITIATED,
                                                  &state_info, &suspend_reason));
  ASSERT_EQ(suspend_reason, DEVICE_SUSPEND_REASON_REBOOT_KERNEL_INITIATED);
  ASSERT_EQ(state_info.performance_state, 7);

  ctx_.loop().Quit();
  ctx_.loop().JoinThreads();
  ASSERT_OK(ctx_.loop().ResetQuit());
  ASSERT_OK(ctx_.loop().RunUntilIdle());

  dev->set_flag(DEV_FLAG_DEAD);
  {
    fbl::AutoLock lock(&ctx_.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx_.DriverManagerRemove(std::move(dev));
  }
  ASSERT_OK(ctx_.loop().RunUntilIdle());
}

TEST_F(CoreTest, RebindHasOneChild) {
  {
    uint32_t unbind_count = 0;
    fbl::RefPtr<zx_device> parent;

    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { *static_cast<uint32_t*>(ctx) += 1; };

    ASSERT_OK(zx_device::Create(&ctx_, "parent", driver_obj_, &parent));
    ASSERT_NO_FATAL_FAILURE(Connect(parent));
    parent->set_ops(&ops);
    parent->set_ctx(&unbind_count);
    {
      fbl::RefPtr<zx_device> child;
      ASSERT_OK(zx_device::Create(&ctx_, "child", driver_obj_, &child));
      ASSERT_NO_FATAL_FAILURE(Connect(child));
      child->set_ops(&ops);
      child->set_ctx(&unbind_count);
      parent->add_child(child.get());
      child->set_parent(parent);

      EXPECT_EQ(device_rebind(parent.get()), ZX_OK);
      EXPECT_EQ(coordinator_.bind_count(), 0);
      ASSERT_NO_FATAL_FAILURE(UnbindDevice(child));
      EXPECT_EQ(unbind_count, 1);

      child->set_flag(DEV_FLAG_DEAD);
    }

    ctx_.loop().Quit();
    ctx_.loop().JoinThreads();
    ASSERT_OK(ctx_.loop().ResetQuit());
    ASSERT_OK(ctx_.loop().RunUntilIdle());
    EXPECT_EQ(coordinator_.bind_count(), 1);

    parent->set_flag(DEV_FLAG_DEAD);
    {
      fbl::AutoLock lock(&ctx_.api_lock());
      parent->removal_cb = [](zx_status_t) {};
      ctx_.DriverManagerRemove(std::move(parent));
    }
    ASSERT_OK(ctx_.loop().RunUntilIdle());
  }
  // Join the thread running in the background, then run the rest of the tasks locally.
}

TEST_F(CoreTest, RebindHasMultipleChildren) {
  {
    uint32_t unbind_count = 0;
    fbl::RefPtr<zx_device> parent;

    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { *static_cast<uint32_t*>(ctx) += 1; };

    ASSERT_OK(zx_device::Create(&ctx_, "parent", driver_obj_, &parent));
    ASSERT_NO_FATAL_FAILURE(Connect(parent));
    parent->set_ops(&ops);
    parent->set_ctx(&unbind_count);
    {
      std::array<fbl::RefPtr<zx_device>, 5> children;
      for (auto& child : children) {
        ASSERT_OK(zx_device::Create(&ctx_, "child", driver_obj_, &child));
        ASSERT_NO_FATAL_FAILURE(Connect(child));
        child->set_ops(&ops);
        child->set_ctx(&unbind_count);
        parent->add_child(child.get());
        child->set_parent(parent);
      }

      EXPECT_EQ(device_rebind(parent.get()), ZX_OK);

      for (auto& child : children) {
        EXPECT_EQ(coordinator_.bind_count(), 0);
        ASSERT_NO_FATAL_FAILURE(UnbindDevice(child));
      }

      EXPECT_EQ(unbind_count, children.size());

      for (auto& child : children) {
        child->set_flag(DEV_FLAG_DEAD);
      }
    }
    // Join the thread running in the background, then run the rest of the tasks locally.
    ctx_.loop().Quit();
    ctx_.loop().JoinThreads();
    ctx_.loop().ResetQuit();
    ctx_.loop().RunUntilIdle();
    EXPECT_EQ(coordinator_.bind_count(), 1);

    parent->set_flag(DEV_FLAG_DEAD);
    {
      fbl::AutoLock lock(&ctx_.api_lock());
      parent->removal_cb = [](zx_status_t) {};
      ctx_.DriverManagerRemove(std::move(parent));
    }
    ASSERT_OK(ctx_.loop().RunUntilIdle());
  }
}

TEST_F(CoreTest, AddDeviceGroup) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx_, "test", driver_obj_, &dev));

  zx_protocol_device_t ops = {};
  dev->set_ops(&ops);

  ASSERT_NO_FATAL_FAILURE(Connect(dev));

  TestAddDeviceGroupCallback test_callback =
      [](fuchsia_device_manager::wire::DeviceGroupDescriptor device_group) {
        ASSERT_EQ(2, device_group.nodes.count());

        // Check the first node.
        auto node_result_1 = device_group.nodes.at(0);
        ASSERT_EQ(2, node_result_1.bind_rules.count());

        auto node_1_bind_rule_1_result = node_result_1.bind_rules.at(0);
        EXPECT_EQ(2, node_1_bind_rule_1_result.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kAccept, node_result_1.bind_rules.at(0).condition);
        ASSERT_EQ(2, node_1_bind_rule_1_result.values.count());
        EXPECT_EQ(1, node_1_bind_rule_1_result.values.at(0).int_value());
        EXPECT_EQ(30, node_1_bind_rule_1_result.values.at(1).int_value());

        auto node_1_bind_rule_2_result = node_result_1.bind_rules.at(1);
        EXPECT_EQ(10, node_1_bind_rule_2_result.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kReject, node_result_1.bind_rules.at(1).condition);
        ASSERT_EQ(1, node_1_bind_rule_2_result.values.count());
        EXPECT_EQ(3, node_1_bind_rule_2_result.values.at(0).int_value());

        auto node_1_bind_props_result = node_result_1.bind_properties;
        EXPECT_EQ(2, node_1_bind_props_result.count());
        ASSERT_EQ(100, node_1_bind_props_result.at(0).key().int_value());
        ASSERT_FALSE(node_1_bind_props_result.at(0).value().bool_value());
        ASSERT_STREQ("kinglet", node_1_bind_props_result.at(1).key().string_value());
        ASSERT_EQ(20, node_1_bind_props_result.at(1).value().int_value());

        // Check the second node.
        auto node_result_2 = device_group.nodes.at(1);
        ASSERT_EQ(2, node_result_2.bind_rules.count());

        auto node_2_bind_rule_1 = node_result_2.bind_rules.at(0);
        EXPECT_EQ(12, node_2_bind_rule_1.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kReject, node_2_bind_rule_1.condition);
        ASSERT_EQ(1, node_2_bind_rule_1.values.count());
        EXPECT_EQ(false, node_2_bind_rule_1.values.at(0).bool_value());

        auto node_2_bind_rule_2 = node_result_2.bind_rules.at(1);
        EXPECT_STREQ("curlew", node_2_bind_rule_2.key.string_value().get());
        EXPECT_EQ(fdf::wire::Condition::kReject, node_2_bind_rule_2.condition);
        ASSERT_EQ(2, node_2_bind_rule_2.values.count());
        EXPECT_STREQ("willet", node_2_bind_rule_2.values.at(0).string_value().get());
        EXPECT_STREQ("sanderling", node_2_bind_rule_2.values.at(1).string_value().get());

        auto node_2_bind_prop_result = node_result_2.bind_properties;
        EXPECT_EQ(1, node_2_bind_prop_result.count());
        ASSERT_EQ(100, node_2_bind_prop_result.at(0).key().int_value());
        ASSERT_TRUE(node_2_bind_prop_result.at(0).value().bool_value());
      };

  coordinator_.set_device_group_callback(std::move(test_callback));

  const device_bind_prop_value_t node_1_bind_rules_values_1[] = {
      device_bind_prop_int_val(1),
      device_bind_prop_int_val(30),
  };

  const device_bind_prop_value_t node_1_bind_rules_values_2[] = {
      device_bind_prop_int_val(3),
  };

  const device_group_bind_rule_t node_1_bind_rules[] = {
      {
          .key = device_bind_prop_int_key(2),
          .condition = DEVICE_BIND_RULE_CONDITION_ACCEPT,
          .values = node_1_bind_rules_values_1,
          .values_count = std::size(node_1_bind_rules_values_1),
      },
      {
          .key = device_bind_prop_int_key(10),
          .condition = DEVICE_BIND_RULE_CONDITION_REJECT,
          .values = node_1_bind_rules_values_2,
          .values_count = std::size(node_1_bind_rules_values_2),
      },
  };

  const device_bind_prop_t node_1_bind_properties[] = {
      {
          .key = device_bind_prop_int_key(100),
          .value = device_bind_prop_bool_val(false),
      },

      {
          .key = device_bind_prop_str_key("kinglet"),
          .value = device_bind_prop_int_val(20),
      }};

  const device_group_node_t node_1{
      .bind_rules = node_1_bind_rules,
      .bind_rule_count = std::size(node_1_bind_rules),
      .bind_properties = node_1_bind_properties,
      .bind_property_count = std::size(node_1_bind_properties),
  };

  const device_bind_prop_value_t node_2_props_values_1[] = {
      device_bind_prop_bool_val(false),
  };

  const device_bind_prop_value_t node_2_props_values_2[] = {
      device_bind_prop_str_val("willet"),
      device_bind_prop_str_val("sanderling"),
  };

  const device_group_bind_rule_t node_2_props[] = {
      {
          .key = device_bind_prop_int_key(12),
          .condition = DEVICE_BIND_RULE_CONDITION_REJECT,
          .values = node_2_props_values_1,
          .values_count = std::size(node_2_props_values_1),
      },
      {
          .key = device_bind_prop_str_key("curlew"),
          .condition = DEVICE_BIND_RULE_CONDITION_REJECT,
          .values = node_2_props_values_2,
          .values_count = std::size(node_2_props_values_2),
      },
  };

  const device_bind_prop_t node_2_bind_properties[] = {{
      .key = device_bind_prop_int_key(100),
      .value = device_bind_prop_bool_val(true),
  }};

  const device_group_node_t node_2{
      .bind_rules = node_2_props,
      .bind_rule_count = std::size(node_2_props),
      .bind_properties = node_2_bind_properties,
      .bind_property_count = std::size(node_2_bind_properties),
  };

  const device_group_node_t nodes[] = {
      node_1,
      node_2,
  };

  const device_group_desc group_desc = {
      .nodes = nodes,
      .nodes_count = std::size(nodes),
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  EXPECT_EQ(ZX_OK, device_add_group(dev.get(), "device_group", &group_desc));

  // Join the thread running in the background, then run the rest of the tasks locally.
  ctx_.loop().Quit();
  ctx_.loop().JoinThreads();
  ctx_.loop().ResetQuit();
  ctx_.loop().RunUntilIdle();

  dev->set_flag(DEV_FLAG_DEAD);
  {
    fbl::AutoLock lock(&ctx_.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx_.DriverManagerRemove(std::move(dev));
  }
  ASSERT_OK(ctx_.loop().RunUntilIdle());
}

}  // namespace
