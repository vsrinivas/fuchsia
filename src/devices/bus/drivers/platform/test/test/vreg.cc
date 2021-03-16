// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/vreg/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-vreg-bind.h"

namespace vreg {

class TestVregDevice;
using DeviceType = ddk::Device<TestVregDevice, ddk::Unbindable>;

class TestVregDevice : public DeviceType,
                       public ddk::VregProtocol<TestVregDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit TestVregDevice(zx_device_t* parent) : DeviceType(parent) {}

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // Vreg Implementation.
  zx_status_t VregSetVoltageStep(uint32_t step);
  uint32_t VregGetVoltageStep();
  void VregGetRegulatorParams(vreg_params_t* out_params);

 private:
  uint32_t step_ = 123;
};

zx_status_t TestVregDevice::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<TestVregDevice>(parent);

  zxlogf(INFO, "TestVregDevice::Create");

  auto status = dev->DdkAdd("test-vreg");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestVregDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestVregDevice::DdkRelease() { delete this; }

zx_status_t TestVregDevice::VregSetVoltageStep(uint32_t step) {
  step_ = step;
  return ZX_OK;
}

uint32_t TestVregDevice::VregGetVoltageStep() { return step_; }

void TestVregDevice::VregGetRegulatorParams(vreg_params_t* out_params) {
  out_params->min_uv = 123;
  out_params->step_size_uv = 456;
  out_params->num_steps = 789;
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = TestVregDevice::Create;
  return driver_ops;
}();

}  // namespace vreg

ZIRCON_DRIVER(test_vreg, vreg::driver_ops, "zircon", "0.1");
