// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/gvnic.h"

#include "src/connectivity/ethernet/drivers/gvnic/gvnic-bind.h"

namespace gvnic {

zx_status_t Gvnic::Bind(void* ctx, zx_device_t* dev) {
  auto driver = std::make_unique<Gvnic>(dev);
  zx_status_t status = driver->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // The DriverFramework now owns driver.
  __UNUSED auto ptr = driver.release();
  return ZX_OK;
}

zx_status_t Gvnic::Bind() {
  is_bound.Set(true);
  return DdkAdd(ddk::DeviceAddArgs("gvnic").set_inspect_vmo(inspect_.DuplicateVmo()));
}

void Gvnic::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Gvnic::DdkRelease() { delete this; }

static zx_driver_ops_t gvnic_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Gvnic::Bind;
  return ops;
}();

}  // namespace gvnic

ZIRCON_DRIVER(Gvnic, gvnic::gvnic_driver_ops, "zircon", "0.1");
