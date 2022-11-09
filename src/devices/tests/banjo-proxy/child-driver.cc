// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <string>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>

#include "src/devices/tests/banjo-proxy/child-driver-bind.h"

namespace {

class Device;
using DeviceParent = ddk::Device<Device, ddk::Unbindable>;

class Device : public DeviceParent {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent) {
    auto device = std::make_unique<Device>(parent);

    auto sysmem = ddk::SysmemProtocolClient(parent, "a");
    if (!sysmem.is_valid()) {
      zxlogf(ERROR, "Sysmem is not valid");
      return ZX_ERR_INTERNAL;
    }

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
      return status;
    }

    status = sysmem.RegisterSecureMem(std::move(server));
    // We want this API to return an odd error so we know we're talking to the right parent.
    if (status != ZX_ERR_STOP) {
      zxlogf(ERROR, "Sysmem::RegisterSecureMem supposed to return ZX_ERR_STOP, but it returned %s",
             zx_status_get_string(status));
      return ZX_ERR_INTERNAL;
    }

    // We've successfully made a banjo call, add a device so the test knows to end.
    status = device->DdkAdd(ddk::DeviceAddArgs("child"));
    if (status == ZX_OK) {
      __UNUSED auto ptr = device.release();
    }
    return status;
  }

  Device(zx_device_t* parent) : DeviceParent(parent) {}

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
};

static constexpr zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(fidl_protocol_test_child, kDriverOps, "zircon", "0.1");
