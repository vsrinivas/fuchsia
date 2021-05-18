// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_DDK_INSTANCE_LIFECYCLE_TEST_TEST_DRIVER_CHILD_H_
#define SRC_DEVICES_TESTS_DDK_INSTANCE_LIFECYCLE_TEST_TEST_DRIVER_CHILD_H_

#include <fuchsia/device/instancelifecycle/test/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

class TestLifecycleDriverChild;
using DeviceType = ddk::Device<TestLifecycleDriverChild, ddk::Unbindable, ddk::MessageableManual,
                               ddk::Openable, ddk::Closable>;
using fuchsia_device_instancelifecycle_test::Lifecycle;

class TestLifecycleDriverChild : public DeviceType {
 public:
  static zx_status_t Create(
      zx_device_t* parent,
      fidl::ServerEnd<fuchsia_device_instancelifecycle_test::Lifecycle> lifecycle_client,
      zx::channel instance_client);

  explicit TestLifecycleDriverChild(
      zx_device_t* parent,
      fidl::ServerEnd<fuchsia_device_instancelifecycle_test::Lifecycle> lifecycle_client)
      : DeviceType(parent), lifecycle_(std::move(lifecycle_client)) {}
  ~TestLifecycleDriverChild() = default;

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t DdkOpen(zx_device_t** out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags) {
    ZX_PANIC("DdkClose reached in device that only returns instances\n");
  }

  void DdkMessage(fidl::IncomingMessage&& msg, DdkTransaction& txn) {
    ZX_PANIC("DdkMessage reached in device that only returns instances\n");
  }

 private:
  // Connection to a Lifecycle client
  fidl::WireEventSender<fuchsia_device_instancelifecycle_test::Lifecycle> lifecycle_;
};

using fuchsia_device_instancelifecycle_test::InstanceDevice;
class TestLifecycleDriverChildInstance;
using InstanceDeviceType =
    ddk::Device<TestLifecycleDriverChildInstance, ddk::Unbindable,
                ddk::Messageable<InstanceDevice>::Mixin, ddk::Openable, ddk::Closable>;

class TestLifecycleDriverChildInstance : public InstanceDeviceType,
                                         public fidl::WireServer<InstanceDevice> {
 public:
  TestLifecycleDriverChildInstance(zx_device_t* parent, TestLifecycleDriverChild* parent_ctx)
      : InstanceDeviceType(parent), parent_ctx_(parent_ctx) {}
  void DdkUnbind(ddk::UnbindTxn txn) { ZX_PANIC("DdkUnbind reached in instance device\n"); }
  void DdkRelease();

  zx_status_t DdkOpen(zx_device_t** out, uint32_t flags) {
    ZX_PANIC("DdkOpen reached in instance device\n");
  };
  zx_status_t DdkClose(uint32_t flags);

  // Implementation of InstanceDevice protocol
  void RemoveDevice(RemoveDeviceRequestView request,
                    RemoveDeviceCompleter::Sync& completer) override;
  void SubscribeToLifecycle(SubscribeToLifecycleRequestView request,
                            SubscribeToLifecycleCompleter::Sync& completer) override;

 private:
  // Pointer to the parent context.  It's guaranteed to outlive the instance devices.
  TestLifecycleDriverChild* parent_ctx_;

  // Connection to a Lifecycle client
  fidl::WireEventSender<fuchsia_device_instancelifecycle_test::Lifecycle> lifecycle_;
};

#endif  // SRC_DEVICES_TESTS_DDK_INSTANCE_LIFECYCLE_TEST_TEST_DRIVER_CHILD_H_
