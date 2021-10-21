// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/composite-driver-v1/composite_driver_v1.h"

#include "src/devices/tests/composite-driver-v1/composite_driver_v1-bind.h"

namespace composite_driver_v1 {

zx_status_t CompositeDriverV1::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<CompositeDriverV1>(dev);
  zx_status_t status = device->Bind();
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

zx_status_t CompositeDriverV1::Bind() {
  is_bound.Set(true);
  return DdkAdd(ddk::DeviceAddArgs("composite_child").set_inspect_vmo(inspect_.DuplicateVmo()));
}

void CompositeDriverV1::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void CompositeDriverV1::DdkRelease() { delete this; }

static zx_driver_ops_t composite_driver_v1_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CompositeDriverV1::Bind;
  return ops;
}();

}  // namespace composite_driver_v1

ZIRCON_DRIVER(CompositeDriverV1, composite_driver_v1::composite_driver_v1_driver_ops, "zircon",
              "0.1");
