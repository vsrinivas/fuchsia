// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/instancelifecycle/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

#include "src/devices/tests/ddk-instance-lifecycle-test/test-driver-child.h"
#include "src/devices/tests/ddk-instance-lifecycle-test/test-lifecycle-bind.h"

namespace {

using fuchsia_device_instancelifecycle_test::Lifecycle;
using fuchsia_device_instancelifecycle_test::TestDevice;

class TestLifecycleDriver;
using DeviceType = ddk::Device<TestLifecycleDriver, ddk::Messageable<TestDevice>::Mixin>;

class TestLifecycleDriver : public DeviceType {
 public:
  explicit TestLifecycleDriver(zx_device_t* parent) : DeviceType(parent) {}
  ~TestLifecycleDriver() {}

  // Device protocol implementation.
  void DdkRelease() { delete this; }

  // Device message ops implementation.
  void CreateDevice(CreateDeviceRequestView request,
                    CreateDeviceCompleter::Sync& completer) override;
};

void TestLifecycleDriver::CreateDevice(CreateDeviceRequestView request,
                                       CreateDeviceCompleter::Sync& completer) {
  zx_status_t status = TestLifecycleDriverChild::Create(zxdev(), std::move(request->lifecycle),
                                                        std::move(request->client_remote));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

zx_status_t TestLifecycleBind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<TestLifecycleDriver>(device);
  auto status = dev->DdkAdd("instance-test");
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
