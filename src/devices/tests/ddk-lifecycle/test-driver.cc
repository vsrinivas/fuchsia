// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/lifecycle/test/llcpp/fidl.h>

#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "test-driver-child.h"
#include "zircon/errors.h"

namespace {

using llcpp::fuchsia::device::lifecycle::test::Lifecycle;
using llcpp::fuchsia::device::lifecycle::test::TestDevice;

class TestLifecycleDriver;
using DeviceType = ddk::Device<TestLifecycleDriver, ddk::UnbindableNew, ddk::Messageable,
                               ddk::ChildPreReleaseable>;

class TestLifecycleDriver : public DeviceType, public TestDevice::Interface {
 public:
  explicit TestLifecycleDriver(zx_device_t* parent) : DeviceType(parent) {}
  ~TestLifecycleDriver() {}

  zx_status_t Bind() { return DdkAdd("ddk-lifecycle-test"); }

  // Device protocol implementation.
  void DdkChildPreRelease(void* child_ctx);
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Device message ops implementation.
  void SubscribeToLifecycle(zx::channel client,
                            SubscribeToLifecycleCompleter::Sync completer) override;
  void AddChild(bool complete_init, int32_t init_status,
                AddChildCompleter::Sync completer) override;
  void RemoveChild(uint64_t child_id, RemoveChildCompleter::Sync completer) override;
  void CompleteChildInit(uint64_t child_id, CompleteChildInitCompleter::Sync completer) override;

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  // Converts the device pointer into an id we can use as a unique identifier.
  uint64_t zxdev_to_id(zx_device_t* dev) { return reinterpret_cast<uint64_t>(dev); }

  zx::channel client_channel_;
  // Child devices added via |AddChild|.
  std::vector<fbl::RefPtr<TestLifecycleDriverChild>> children_;
};

void TestLifecycleDriver::DdkChildPreRelease(void* child_ctx) {
  auto child = reinterpret_cast<TestLifecycleDriverChild*>(child_ctx);
  ZX_ASSERT(child != nullptr);
  auto id = zxdev_to_id(child->zxdev());

  if (client_channel_) {
    zx_status_t status = Lifecycle::SendOnChildPreReleaseEvent(zx::unowned(client_channel_), id);
    ZX_ASSERT(status == ZX_OK);
  }
  // Remove the child from our |children_| vector.
  auto child_matcher = [&](fbl::RefPtr<TestLifecycleDriverChild> child_to_remove) {
    return child_to_remove.get() == child;
  };
  children_.erase(std::remove_if(children_.begin(), children_.end(), child_matcher),
                  children_.end());
}

void TestLifecycleDriver::AddChild(bool complete_init, int32_t init_status,
                                   AddChildCompleter::Sync completer) {
  fbl::RefPtr<TestLifecycleDriverChild> child;
  zx_status_t status =
      TestLifecycleDriverChild::Create(zxdev(), complete_init, init_status, &child);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    children_.push_back(child);
    completer.ReplySuccess(zxdev_to_id(child->zxdev()));
  }
}

void TestLifecycleDriver::RemoveChild(uint64_t id, RemoveChildCompleter::Sync completer) {
  bool found = false;
  for (auto& child : children_) {
    if (zxdev_to_id(child->zxdev()) == id) {
      // We will remove it from our |children_| vector when we get the child pre-release callback.
      child->DdkAsyncRemove();
      found = true;
      break;
    }
  }
  if (!found) {
    zxlogf(ERROR, "Could not find child: id %lu\n", id);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }
  completer.ReplySuccess();
}

void TestLifecycleDriver::CompleteChildInit(uint64_t id,
                                            CompleteChildInitCompleter::Sync completer) {
  zx_status_t status = ZX_ERR_NOT_FOUND;
  for (auto& child : children_) {
    if (zxdev_to_id(child->zxdev()) == id) {
      status = child->CompleteInit();
      break;
    }
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to complete child init: id %lu\n", id);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void TestLifecycleDriver::SubscribeToLifecycle(zx::channel client,
                                               SubscribeToLifecycleCompleter::Sync completer) {
  // Currently we only care about supporting one client.
  if (client_channel_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
  } else {
    client_channel_ = std::move(client);
    completer.ReplySuccess();
  }
}

zx_status_t TestLifecycleBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestLifecycleDriver>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestLifecycleBind;
  return ops;
}();

}  // namespace

// clang-format off
ZIRCON_DRIVER_BEGIN(TestLifecycle, driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_LIFECYCLE_TEST),
ZIRCON_DRIVER_END(TestLifecycle)
    // clang-format on
