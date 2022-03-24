// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_POWER_SENSOR_H_
#define SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_POWER_SENSOR_H_

#include <fidl/fuchsia.hardware.power.sensor/cpp/wire.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"

namespace chromiumos_ec_core::power_sensor {

constexpr size_t kAtlasAdcPsysChannel = 1;

class CrOsEcPowerSensorDevice;

using CrOsEcPowerSensorDeviceType =
    ddk::Device<CrOsEcPowerSensorDevice,
                ddk::Messageable<fuchsia_hardware_power_sensor::Device>::Mixin, ddk::Initializable>;

class CrOsEcPowerSensorDevice : public CrOsEcPowerSensorDeviceType {
 public:
  // Create and bind the device.
  //
  // A pointer to the created device will be placed in |device|, though ownership
  // remains with the DDK. Any use of |device| must occur before DdkRelease()
  // is called.
  static zx_status_t Bind(zx_device_t* parent, ChromiumosEcCore* ec,
                          CrOsEcPowerSensorDevice** device);

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // fuchsia.hardware.power.sensor methods
  void GetPowerWatts(GetPowerWattsRequestView request,
                     GetPowerWattsCompleter::Sync& completer) override;
  void GetVoltageVolts(GetVoltageVoltsRequestView request,
                       GetVoltageVoltsCompleter::Sync& completer) override;

 private:
  CrOsEcPowerSensorDevice(ChromiumosEcCore* ec, zx_device_t* parent)
      : CrOsEcPowerSensorDeviceType(parent), ec_(ec) {}
  DISALLOW_COPY_ASSIGN_AND_MOVE(CrOsEcPowerSensorDevice);

  fpromise::promise<void, zx_status_t> UpdateState();

  ChromiumosEcCore* ec_;

  float power_;
};

}  // namespace chromiumos_ec_core::power_sensor

#endif  // SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_POWER_SENSOR_H_
