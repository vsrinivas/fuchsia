// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/lifecycle/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>

#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/ddk-lifecycle/test-driver-child.h"
#include "src/devices/tests/ddk-lifecycle/test-lifecycle-bind.h"

namespace {

using fuchsia_device_lifecycle_test::Lifecycle;
using fuchsia_device_lifecycle_test::TestDevice;

class TestLifecycleDriver;
using DeviceType =
    ddk::Device<TestLifecycleDriver, ddk::Unbindable, ddk::Messageable, ddk::ChildPreReleaseable>;

class TestLifecycleDriver : public DeviceType, public TestDevice::Interface {
 public:
  explicit TestLifecycleDriver(zx_device_t* parent) : DeviceType(parent) {}
  ~TestLifecycleDriver() {}

  zx_status_t Bind() { return DdkAdd("ddk-lifecycle-test"); }

  // Device protocol implementation.
  void DdkChildPreRelease(void* child_ctx);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Device message ops implementation.
  void SubscribeToLifecycle(fidl::ServerEnd<Lifecycle> client,
                            SubscribeToLifecycleCompleter::Sync& completer) override;
  void AddChild(bool complete_init, int32_t init_status,
                AddChildCompleter::Sync& completer) override;
  void RemoveChild(uint64_t child_id, RemoveChildCompleter::Sync& completer) override;
  void AsyncRemoveChild(uint64_t child_id, AsyncRemoveChildCompleter::Sync& completer) override;
  void CompleteUnbind(uint64_t child_id, CompleteUnbindCompleter::Sync& completer) override;
  void CompleteChildInit(uint64_t child_id, CompleteChildInitCompleter::Sync& completer) override;

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fidl::WireDispatch<TestDevice>(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  // Converts the device pointer into an id we can use as a unique identifier.
  uint64_t zxdev_to_id(zx_device_t* dev) { return reinterpret_cast<uint64_t>(dev); }

  Lifecycle::EventSender lifecycle_event_sender_;
  // Child devices added via |AddChild|.
  std::vector<fbl::RefPtr<TestLifecycleDriverChild>> children_;
};

void TestLifecycleDriver::DdkChildPreRelease(void* child_ctx) {
  auto child = reinterpret_cast<TestLifecycleDriverChild*>(child_ctx);
  ZX_ASSERT(child != nullptr);
  auto id = zxdev_to_id(child->zxdev());

  if (lifecycle_event_sender_.is_valid()) {
    zx_status_t status = lifecycle_event_sender_.OnChildPreRelease(id);
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
                                   AddChildCompleter::Sync& completer) {
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

void TestLifecycleDriver::RemoveChild(uint64_t id, RemoveChildCompleter::Sync& completer) {
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
    zxlogf(ERROR, "Could not find child: id %lu", id);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }
  completer.ReplySuccess();
}

void TestLifecycleDriver::AsyncRemoveChild(uint64_t id,
                                           AsyncRemoveChildCompleter::Sync& completer) {
  bool found = false;
  for (auto& child : children_) {
    if (zxdev_to_id(child->zxdev()) == id) {
      // We will remove it from our |children_| vector when we get the child pre-release callback.
      child->AsyncRemove(
          [completion = completer.ToAsync()]() mutable { completion.ReplySuccess(); });
      found = true;
      break;
    }
  }
  if (!found) {
    zxlogf(ERROR, "Could not find child: id %lu", id);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }
}

void TestLifecycleDriver::CompleteUnbind(uint64_t child_id,
                                         CompleteUnbindCompleter::Sync& completer) {
  for (auto& child : children_) {
    if (zxdev_to_id(child->zxdev()) == child_id) {
      // We will remove it from our |children_| vector when we get the child pre-release callback.
      child->CompleteUnbind();
      completer.ReplySuccess();
      return;
    }
  }
  zxlogf(ERROR, "Could not find child: id %lu", child_id);
  completer.ReplyError(ZX_ERR_NOT_FOUND);
  return;
}

void TestLifecycleDriver::CompleteChildInit(uint64_t id,
                                            CompleteChildInitCompleter::Sync& completer) {
  zx_status_t status = ZX_ERR_NOT_FOUND;
  for (auto& child : children_) {
    if (zxdev_to_id(child->zxdev()) == id) {
      status = child->CompleteInit();
      break;
    }
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to complete child init: id %lu", id);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void TestLifecycleDriver::SubscribeToLifecycle(fidl::ServerEnd<Lifecycle> client,
                                               SubscribeToLifecycleCompleter::Sync& completer) {
  // Currently we only care about supporting one client.
  if (lifecycle_event_sender_.is_valid()) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
  } else {
    lifecycle_event_sender_ = Lifecycle::EventSender(std::move(client));
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

ZIRCON_DRIVER(TestLifecycle, driver_ops, "zircon", "0.1");
