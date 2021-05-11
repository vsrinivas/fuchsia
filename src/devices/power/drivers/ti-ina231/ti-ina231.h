// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_
#define SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_

#include <fuchsia/hardware/power/sensor/llcpp/fidl.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zx/status.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace power_sensor {

namespace power_sensor_fidl = fuchsia_hardware_power_sensor;

class Ina231Device;
using DeviceType = ddk::Device<Ina231Device, ddk::Messageable<power_sensor_fidl::Device>::Mixin>;

class Ina231Device : public DeviceType,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_POWER_SENSOR>,
                     public fidl::WireServer<power_sensor_fidl::Device> {
 public:
  Ina231Device(zx_device_t* parent, uint32_t shunt_resistor_uohms, ddk::I2cChannel i2c)
      : DeviceType(parent), shunt_resistor_uohms_(shunt_resistor_uohms), i2c_(i2c) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  void GetPowerWatts(GetPowerWattsRequestView request,
                     GetPowerWattsCompleter::Sync& completer) override;

  // Visible for testing.
  zx_status_t Init();

 private:
  enum class Register : uint8_t;

  zx::status<uint16_t> Read16(Register reg);
  zx::status<> Write16(Register reg, uint16_t value);

  const uint32_t shunt_resistor_uohms_;
  ddk::I2cChannel i2c_;
};

}  // namespace power_sensor

#endif  // SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_
