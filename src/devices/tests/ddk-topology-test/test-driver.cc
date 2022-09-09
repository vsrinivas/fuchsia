// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/ddk-topology-test/test-driver-bind.h"

namespace {

class TestTopologyDriver;
using DeviceType = ddk::Device<TestTopologyDriver>;

class TestTopologyDriver : public DeviceType {
 public:
  explicit TestTopologyDriver(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Bind() {
    // Generate a unique child device name in case the driver is bound multiple
    // times.
    zx_status_t status =
        DdkAdd(ddk::DeviceAddArgs("topology-grandparent").set_flags(DEVICE_ADD_NON_BINDABLE));
    if (status != ZX_OK) {
      return status;
    }

    static const zx_protocol_device_t kEmptyDeviceOps{
        .version = DEVICE_OPS_VERSION,
        .release = [](void*) {},
    };
    // Add two immediate children.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = nullptr,
        .ops = &kEmptyDeviceOps,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };
    zx_device_t* parent1;
    zx_device_t* parent2;
    args.name = "parent1";
    status = device_add(zxdev(), &args, &parent1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add parent1: %s", zx_status_get_string(status));
      return status;
    }

    args.name = "parent2";
    status = device_add(zxdev(), &args, &parent2);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add parent2: %s", zx_status_get_string(status));
      return status;
    }

    // Now add a device with the same name in two locations.
    args.name = "child";
    args.flags = 0;
    status = device_add(parent1, &args, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add first child: %s", zx_status_get_string(status));
    }

    status = device_add(parent2, &args, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add second child: %s", zx_status_get_string(status));
    }

    return ZX_OK;
  }

  // Device protocol implementation.
  void DdkRelease() { delete this; }
};

zx_status_t TestTopologyBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestTopologyDriver>(&ac, device);
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
  ops.bind = TestTopologyBind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(TestTopology, driver_ops, "zircon", "0.1");
