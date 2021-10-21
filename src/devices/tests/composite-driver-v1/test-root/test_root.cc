// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/composite-driver-v1/test-root/test_root.h"

#include "src/devices/tests/composite-driver-v1/test-root/test_root-bind.h"

namespace test_root {

zx_status_t TestRoot::Bind(void* ctx, zx_device_t* dev) {
  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 1},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_a", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 2},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_b", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 3},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_c", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  return ZX_OK;
}

zx_status_t TestRoot::Bind(const char* name, cpp20::span<const zx_device_prop_t> props) {
  is_bound.Set(true);
  return DdkAdd(ddk::DeviceAddArgs(name).set_props(props).set_inspect_vmo(inspect_.DuplicateVmo()));
}

void TestRoot::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void TestRoot::DdkRelease() { delete this; }

static zx_driver_ops_t test_root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestRoot::Bind;
  return ops;
}();

}  // namespace test_root

ZIRCON_DRIVER(TestRoot, test_root::test_root_driver_ops, "zircon", "0.1");
