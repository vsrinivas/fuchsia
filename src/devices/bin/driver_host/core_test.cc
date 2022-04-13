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
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override {
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
  void ScheduleUnbindChildren(ScheduleUnbindChildrenRequestView request,
                              ScheduleUnbindChildrenCompleter::Sync& completer) override {}

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

TEST_F(CoreTest, RebindHasOneChild) {
  {
    uint32_t unbind_count = 0;
    fbl::RefPtr<zx_device> parent;

    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { *static_cast<uint32_t*>(ctx) += 1; };

    ASSERT_OK(zx_device::Create(&ctx_, "parent", driver_obj_, &parent));
    ASSERT_NO_FATAL_FAILURE(Connect(parent));
    parent->set_ops(&ops);
    parent->ctx = &unbind_count;
    {
      fbl::RefPtr<zx_device> child;
      ASSERT_OK(zx_device::Create(&ctx_, "child", driver_obj_, &child));
      ASSERT_NO_FATAL_FAILURE(Connect(child));
      child->set_ops(&ops);
      child->ctx = &unbind_count;
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
    parent->ctx = &unbind_count;
    {
      std::array<fbl::RefPtr<zx_device>, 5> children;
      for (auto& child : children) {
        ASSERT_OK(zx_device::Create(&ctx_, "child", driver_obj_, &child));
        ASSERT_NO_FATAL_FAILURE(Connect(child));
        child->set_ops(&ops);
        child->ctx = &unbind_count;
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
        // Check the device group properties.
        EXPECT_EQ(1, device_group.props.count());
        EXPECT_EQ(100, device_group.props.at(0).id);
        EXPECT_EQ(10, device_group.props.at(0).value);

        // Check the device group string properties.
        ASSERT_EQ(1, device_group.str_props.count());
        EXPECT_STREQ("plover", device_group.str_props.at(0).key.get());
        EXPECT_EQ(10, device_group.str_props.at(0).value.int_value());

        ASSERT_EQ(2, device_group.fragments.count());

        // Checking the first fragment.
        auto fragment_result_1 = device_group.fragments.at(0);
        EXPECT_STREQ("fragment-1", fragment_result_1.name.get());
        ASSERT_EQ(2, fragment_result_1.properties.count());

        auto fragment_1_prop_1_result = fragment_result_1.properties.at(0);
        EXPECT_EQ(2, fragment_1_prop_1_result.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kAccept, fragment_result_1.properties.at(0).condition);
        ASSERT_EQ(2, fragment_1_prop_1_result.values.count());
        EXPECT_EQ(1, fragment_1_prop_1_result.values.at(0).int_value());
        EXPECT_EQ(30, fragment_1_prop_1_result.values.at(1).int_value());

        auto fragment_1_prop_2_result = fragment_result_1.properties.at(1);
        EXPECT_EQ(10, fragment_1_prop_2_result.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kReject, fragment_result_1.properties.at(1).condition);
        ASSERT_EQ(1, fragment_1_prop_2_result.values.count());
        EXPECT_EQ(3, fragment_1_prop_2_result.values.at(0).int_value());

        // Checking the second fragment.
        auto fragment_result_2 = device_group.fragments.at(1);
        EXPECT_STREQ("fragment-2", fragment_result_2.name.get());
        ASSERT_EQ(2, fragment_result_2.properties.count());

        auto fragment_2_property_1 = fragment_result_2.properties.at(0);
        EXPECT_EQ(12, fragment_2_property_1.key.int_value());
        EXPECT_EQ(fdf::wire::Condition::kReject, fragment_2_property_1.condition);
        ASSERT_EQ(1, fragment_2_property_1.values.count());
        EXPECT_EQ(false, fragment_2_property_1.values.at(0).bool_value());

        auto fragment_2_property_2 = fragment_result_2.properties.at(1);
        EXPECT_STREQ("curlew", fragment_2_property_2.key.string_value().get());
        EXPECT_EQ(fdf::wire::Condition::kReject, fragment_2_property_2.condition);
        ASSERT_EQ(2, fragment_2_property_2.values.count());
        EXPECT_STREQ("willet", fragment_2_property_2.values.at(0).string_value().get());
        EXPECT_STREQ("sanderling", fragment_2_property_2.values.at(1).string_value().get());
      };

  coordinator_.set_device_group_callback(std::move(test_callback));

  const zx_device_str_prop_val_t fragment_1_props_values_1[] = {
      str_prop_int_val(1),
      str_prop_int_val(30),
  };

  const zx_device_str_prop_val_t fragment_1_props_values_2[] = {
      str_prop_int_val(3),
  };

  const device_group_prop_t fragment_1_props[] = {
      device_group_prop_t{
          .key = device_group_prop_int_key(2),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_ACCEPT,
          .values = fragment_1_props_values_1,
          .values_count = std::size(fragment_1_props_values_1),
      },
      device_group_prop_t{
          .key = device_group_prop_int_key(10),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_REJECT,
          .values = fragment_1_props_values_2,
          .values_count = std::size(fragment_1_props_values_2),
      },
  };

  const device_group_fragment fragment_1{
      .name = "fragment-1",
      .props = fragment_1_props,
      .props_count = std::size(fragment_1_props),
  };

  const zx_device_str_prop_val_t fragment_2_props_values_1[] = {
      str_prop_bool_val(false),
  };

  const zx_device_str_prop_val_t fragment_2_props_values_2[] = {
      str_prop_str_val("willet"),
      str_prop_str_val("sanderling"),
  };

  const device_group_prop_t fragment_2_props[] = {
      device_group_prop_t{
          .key = device_group_prop_int_key(12),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_REJECT,
          .values = fragment_2_props_values_1,
          .values_count = std::size(fragment_2_props_values_1),
      },
      device_group_prop_t{
          .key = device_group_prop_str_key("curlew"),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_REJECT,
          .values = fragment_2_props_values_2,
          .values_count = std::size(fragment_2_props_values_2),
      },
  };

  const device_group_fragment fragment_2{
      .name = "fragment-2",
      .props = fragment_2_props,
      .props_count = std::size(fragment_2_props),
  };

  const device_group_fragment fragments[] = {
      fragment_1,
      fragment_2,
  };

  const zx_device_prop_t group_props[] = {
      {100, 0, 10},
  };

  const zx_device_str_prop group_str_props[] = {
      {"plover", str_prop_int_val(10)},
  };

  const device_group_desc group_desc = {
      .fragments = fragments,
      .fragments_count = std::size(fragments),
      .props = group_props,
      .props_count = std::size(group_props),
      .str_props = group_str_props,
      .str_props_count = std::size(group_str_props),
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
