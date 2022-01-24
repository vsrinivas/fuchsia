// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/misc/drivers/test-parent/test-parent-bind.h"

namespace {

class TestParent;
using TestParentType = ddk::Device<TestParent>;

class TestParent : public TestParentType {
 public:
  explicit TestParent(zx_device_t* device) : TestParentType(device) {}

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease() { delete this; }
};

zx_status_t TestParent::Create(zx_device_t* parent) {
  auto test_parent = std::make_unique<TestParent>(parent);
  zx_status_t status = test_parent->DdkAdd(ddk::DeviceAddArgs("test")
                                               .set_proto_id(ZX_PROTOCOL_TEST_PARENT)
                                               .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE));
  if (status != ZX_OK) {
    return status;
  }

  // Now owned by the driver framework.
  __UNUSED auto ptr = test_parent.release();

  return ZX_OK;
}

class SysDevice;
using SysDeviceType = ddk::Device<SysDevice>;

class SysDevice : public SysDeviceType {
 public:
  explicit SysDevice(zx_device_t* device) : SysDeviceType(device) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                            zx_handle_t items_svc_handle);

  // Device protocol implementation.
  void DdkRelease() { delete this; }
};

zx_status_t SysDevice::Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                              zx_handle_t items_svc_handle) {
  auto sys_device = std::make_unique<SysDevice>(parent);
  zx_status_t status =
      sys_device->DdkAdd(ddk::DeviceAddArgs("sys").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  // Now owned by the driver framework.
  auto ptr = sys_device.release();
  return TestParent::Create(ptr->zxdev());
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = SysDevice::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(test - parent, driver_ops, "zircon", "0.1");
