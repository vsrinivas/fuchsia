// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <ddk/driver.h>
#include <zxtest/zxtest.h>

#include "driver_host.h"
#include "zx_device.h"

namespace {

class FakeCoordinator : public ::llcpp::fuchsia::device::manager::Coordinator::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::Bind(dispatcher, std::move(request), this);
  }

  void AddDevice(::zx::channel coordinator, ::zx::channel device_controller,
                 ::fidl::VectorView<llcpp::fuchsia::device::manager::DeviceProperty> props,
                 ::fidl::StringView name, uint32_t protocol_id, ::fidl::StringView driver_path,
                 ::fidl::StringView args,
                 llcpp::fuchsia::device::manager::AddDeviceConfig device_add_config, bool has_init,
                 ::zx::channel client_remote, AddDeviceCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_AddDevice_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void ScheduleRemove(bool unbind_self, ScheduleRemoveCompleter::Sync completer) override {}
  void AddCompositeDevice(::fidl::StringView name,
                          llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc,
                          AddCompositeDeviceCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_AddCompositeDevice_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void PublishMetadata(::fidl::StringView device_path, uint32_t key,
                       ::fidl::VectorView<uint8_t> data,
                       PublishMetadataCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_PublishMetadata_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void AddDeviceInvisible(::zx::channel coordinator, ::zx::channel device_controller,
                          ::fidl::VectorView<llcpp::fuchsia::device::manager::DeviceProperty> props,
                          ::fidl::StringView name, uint32_t protocol_id,
                          ::fidl::StringView driver_path, ::fidl::StringView args, bool has_init,
                          ::zx::channel client_remote,
                          AddDeviceInvisibleCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_AddDeviceInvisible_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void MakeVisible(MakeVisibleCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_MakeVisible_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void BindDevice(::fidl::StringView driver_path, BindDeviceCompleter::Sync completer) override {
    bind_count_++;
    llcpp::fuchsia::device::manager::Coordinator_BindDevice_Result response;
    zx_status_t status = ZX_OK;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_GetTopologicalPath_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void LoadFirmware(::fidl::StringView fw_path, LoadFirmwareCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_LoadFirmware_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void GetMetadata(uint32_t key, GetMetadataCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_GetMetadata_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void GetMetadataSize(uint32_t key, GetMetadataSizeCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_GetMetadataSize_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void AddMetadata(uint32_t key, ::fidl::VectorView<uint8_t> data,
                   AddMetadataCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_AddMetadata_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void ScheduleUnbindChildren(ScheduleUnbindChildrenCompleter::Sync completer) override {}
  void RunCompatibilityTests(int64_t hook_wait_time,
                             RunCompatibilityTestsCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_RunCompatibilityTests_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }
  void DirectoryWatch(uint32_t mask, uint32_t options, ::zx::channel watcher,
                      DirectoryWatchCompleter::Sync completer) override {
    llcpp::fuchsia::device::manager::Coordinator_DirectoryWatch_Result response;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    response.set_err(fidl::unowned(&status));
    completer.Reply(std::move(response));
  }

  uint32_t bind_count_ = 0;
  uint32_t unbind_reply_count_ = 0;
};

class CoreTest : public zxtest::Test {
 protected:
  CoreTest() : ctx_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ctx_.loop().StartThread("driver_host-test-loop");
  }

  void Connect(zx::channel* out) {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ASSERT_OK(coordinator_.Connect(ctx_.loop().dispatcher(), std::move(remote)));
    *out = std::move(local);
  }

  // This simulates receiving an unbind and remove request from the devcoordinator.
  void UnbindDevice(fbl::RefPtr<zx_device> dev) {
    ApiAutoLock lock;
    ctx_.DeviceUnbind(dev);
    // DeviceCompleteRemoval() will drop the device reference added by device_add().
    // Since we never called device_add(), we should increment the reference count here.
    fbl::RefPtr<zx_device_t> dev_add_ref(dev.get());
    __UNUSED auto ptr = fbl::ExportToRawPtr(&dev_add_ref);
    ctx_.DeviceCompleteRemoval(dev);
  }

  DriverHostContext ctx_;
  FakeCoordinator coordinator_;
};

TEST_F(CoreTest, RebindNoChildren) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  dev->ops = &ops;

  zx::channel rpc;
  ASSERT_NO_FATAL_FAILURES(Connect(&rpc));
  dev->coordinator_rpc = zx::unowned(rpc);

  EXPECT_EQ(device_rebind(dev.get()), ZX_OK);
  EXPECT_EQ(coordinator_.bind_count_, 1);

  dev->flags |= DEV_FLAG_DEAD;
}

TEST_F(CoreTest, RebindHasOneChild) {
  {
    uint32_t unbind_count = 0;
    fbl::RefPtr<zx_device> parent;

    zx::channel rpc;
    ASSERT_NO_FATAL_FAILURES(Connect(&rpc));

    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { *static_cast<uint32_t*>(ctx) += 1; };

    ASSERT_OK(zx_device::Create(&parent));
    parent->ops = &ops;
    parent->ctx = &unbind_count;
    parent->coordinator_rpc = zx::unowned(rpc);
    {
      fbl::RefPtr<zx_device> child;
      ASSERT_OK(zx_device::Create(&child));
      child->ops = &ops;
      child->ctx = &unbind_count;
      child->rpc = zx::unowned(rpc);
      parent->children.push_back(child.get());
      child->parent = parent;

      EXPECT_EQ(device_rebind(parent.get()), ZX_OK);
      EXPECT_EQ(coordinator_.bind_count_, 0);
      ASSERT_NO_FATAL_FAILURES(UnbindDevice(child));
      EXPECT_EQ(unbind_count, 1);

      child->flags |= DEV_FLAG_DEAD;
    }
    parent->flags |= DEV_FLAG_DEAD;
  }
  EXPECT_EQ(coordinator_.bind_count_, 1);
}

TEST_F(CoreTest, RebindHasMultipleChildren) {
  {
    uint32_t unbind_count = 0;
    fbl::RefPtr<zx_device> parent;

    zx::channel rpc;
    ASSERT_NO_FATAL_FAILURES(Connect(&rpc));

    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { *static_cast<uint32_t*>(ctx) += 1; };

    ASSERT_OK(zx_device::Create(&parent));
    parent->ops = &ops;
    parent->ctx = &unbind_count;
    parent->coordinator_rpc = zx::unowned(rpc);
    {
      std::array<fbl::RefPtr<zx_device>, 5> children;
      for (auto& child : children) {
        ASSERT_OK(zx_device::Create(&child));
        child->ops = &ops;
        child->ctx = &unbind_count;
        child->rpc = zx::unowned(rpc);
        parent->children.push_back(child.get());
        child->parent = parent;
      }

      EXPECT_EQ(device_rebind(parent.get()), ZX_OK);

      for (auto& child : children) {
        EXPECT_EQ(coordinator_.bind_count_, 0);
        ASSERT_NO_FATAL_FAILURES(UnbindDevice(child));
      }

      EXPECT_EQ(unbind_count, children.size());

      for (auto& child : children) {
        child->flags |= DEV_FLAG_DEAD;
      }
    }
    parent->flags |= DEV_FLAG_DEAD;
  }
  EXPECT_EQ(coordinator_.bind_count_, 1);
}

}  // namespace
