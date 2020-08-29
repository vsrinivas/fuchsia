// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/vreg.h>

namespace vreg {

class TestVregDevice;
using DeviceType = ddk::Device<TestVregDevice, ddk::UnbindableNew>;

class TestVregDevice : public DeviceType,
                       public ddk::VregProtocol<TestVregDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit TestVregDevice(zx_device_t* parent) : DeviceType(parent) {}

  void DdkUnbindNew(ddk::UnbindTxn txn);
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

void TestVregDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

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

// clang-format off
ZIRCON_DRIVER_BEGIN(test_vreg, vreg::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_VREG),
ZIRCON_DRIVER_END(test_vreg)
