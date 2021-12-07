// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/drivers/driver_test_realm/sample-driver/sample_driver.h"

#include "examples/drivers/driver_test_realm/sample-driver/sample_driver-bind.h"

namespace sample_driver {

zx_status_t SampleDriver::Bind(void* ctx, zx_device_t* dev) {
  auto driver = std::make_unique<SampleDriver>(dev);
  zx_status_t status = driver->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // The DriverFramework now owns driver.
  __UNUSED auto ptr = driver.release();
  return ZX_OK;
}

zx_status_t SampleDriver::Bind() {
  is_bound.Set(true);
  return DdkAdd(ddk::DeviceAddArgs("sample_driver").set_inspect_vmo(inspect_.DuplicateVmo()));
}

void SampleDriver::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void SampleDriver::DdkRelease() { delete this; }

static zx_driver_ops_t sample_driver_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SampleDriver::Bind;
  return ops;
}();

}  // namespace sample_driver

ZIRCON_DRIVER(SampleDriver, sample_driver::sample_driver_driver_ops, "zircon", "0.1");
