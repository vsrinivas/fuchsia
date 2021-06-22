// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-power-sensor-bind.h"

namespace power_sensor {

class TestPowerSensorDevice;
using DeviceType = ddk::Device<TestPowerSensorDevice>;

class TestPowerSensorDevice
    : public DeviceType,
      public ddk::PowerSensorProtocol<TestPowerSensorDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit TestPowerSensorDevice(zx_device_t* parent) : DeviceType(parent) {}

  void DdkRelease();

  zx_status_t PowerSensorConnectServer(zx::channel server);
};

zx_status_t TestPowerSensorDevice::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<TestPowerSensorDevice>(parent);

  zxlogf(INFO, "TestPowerSensorDevice::Create");

  auto status = dev->DdkAdd("test-power-sensor");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestPowerSensorDevice::DdkRelease() { delete this; }

zx_status_t TestPowerSensorDevice::PowerSensorConnectServer(zx::channel server) {
  return ZX_OK;
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = TestPowerSensorDevice::Create;
  return driver_ops;
}();

}  // namespace power_sensor

ZIRCON_DRIVER(test_power_sensor, power_sensor::driver_ops, "zircon", "0.1");
