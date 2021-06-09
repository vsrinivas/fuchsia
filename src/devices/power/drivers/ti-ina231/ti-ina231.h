// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_
#define SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_

#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <fuchsia/hardware/power/sensor/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "ti-ina231-metadata.h"

namespace power_sensor {

namespace power_sensor_fidl = fuchsia_hardware_power_sensor;

class Ina231Device;
using DeviceType = ddk::Device<Ina231Device, ddk::Messageable<power_sensor_fidl::Device>::Mixin>;

class Ina231Device : public DeviceType,
                     public ddk::PowerSensorProtocol<Ina231Device, ddk::base_protocol> {
 public:
  Ina231Device(zx_device_t* parent, uint32_t shunt_resistor_uohms, ddk::I2cChannel i2c)
      : DeviceType(parent),
        shunt_resistor_uohms_(shunt_resistor_uohms),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        i2c_(i2c) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t PowerSensorConnectServer(zx::channel server);

  void GetPowerWatts(GetPowerWattsRequestView request,
                     GetPowerWattsCompleter::Sync& completer) override;
  void GetVoltageVolts(GetVoltageVoltsRequestView request,
                       GetVoltageVoltsCompleter::Sync& completer) override;

  // Visible for testing.
  zx_status_t Init(const Ina231Metadata& metadata);

 private:
  enum class Register : uint8_t;

  zx::status<uint16_t> Read16(Register reg) TA_REQ(i2c_lock_);
  zx::status<> Write16(Register reg, uint16_t value) TA_REQ(i2c_lock_);

  const uint32_t shunt_resistor_uohms_;
  async::Loop loop_;
  fbl::Mutex i2c_lock_;
  ddk::I2cChannel i2c_ TA_GUARDED(i2c_lock_);
};

}  // namespace power_sensor

#endif  // SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_H_
