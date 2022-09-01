// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/driver-multiname-test/parent_device.h"

#include "src/devices/tests/driver-multiname-test/child_device.h"
#include "src/devices/tests/driver-multiname-test/parent_device-bind.h"

namespace parent_device {

zx_status_t ParentDevice::Bind(void* ctx, zx_device_t* dev) {
  auto driver = std::make_unique<ParentDevice>(dev);
  zx_status_t status = driver->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // The DriverFramework now owns the driver.
  __UNUSED auto ptr = driver.release();
  return ZX_OK;
}

zx_status_t ParentDevice::Bind() { return DdkAdd(ddk::DeviceAddArgs("parent_device")); }

void ParentDevice::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void ParentDevice::DdkRelease() { delete this; }

void ParentDevice::AddDevice(AddDeviceCompleter::Sync& completer) {
  auto child = std::make_unique<child_device::ChildDevice>(zxdev());

  zx_status_t status;
  if ((status = child->DdkAdd("duplicate")) != ZX_OK) {
    completer.ReplyError(status);
  }

  // Release the child as it is owned by the framework now.
  child.release()->DdkRelease();

  completer.ReplySuccess();
}

static zx_driver_ops_t parent_device_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ParentDevice::Bind;
  return ops;
}();

}  // namespace parent_device

ZIRCON_DRIVER(ParentDevice, parent_device::parent_device_driver_ops, "zircon", "0.1");
