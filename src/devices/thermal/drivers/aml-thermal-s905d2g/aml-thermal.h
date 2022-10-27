// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_THERMAL_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_THERMAL_H_

#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <fuchsia/hardware/thermal/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/device.h>
#include <lib/fidl-utils/bind.h>
#include <threads.h>

#include <memory>
#include <utility>

#include <ddktl/device.h>

#include "aml-tsensor.h"

namespace thermal {

class AmlThermal;
using DeviceType =
    ddk::Device<AmlThermal, ddk::Messageable<fuchsia_hardware_thermal::Device>::Mixin>;

class AmlThermal : public DeviceType, public ddk::ThermalProtocol<AmlThermal, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlThermal);
  AmlThermal(zx_device_t* device, std::unique_ptr<thermal::AmlTSensor> tsensor,
             fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config)
      : DeviceType(device),
        tsensor_(std::move(tsensor)),
        thermal_config_(std::move(thermal_config)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  static zx_status_t Create(void* ctx, zx_device_t* device);

  // Ddk Hooks
  void DdkRelease();
  void DdkMessage(fidl::IncomingHeaderAndMessage&& msg, DdkTransaction& txn);

  // Implements ZX_PROTOCOL_THERMAL
  zx_status_t ThermalConnect(zx::channel ch);

 private:
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) override;
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) override;
  void GetDvfsInfo(GetDvfsInfoRequestView request, GetDvfsInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) override;
  void GetStateChangePort(GetStateChangePortCompleter::Sync& completer) override;
  void SetTripCelsius(SetTripCelsiusRequestView request,
                      SetTripCelsiusCompleter::Sync& completer) override;
  void GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                             GetDvfsOperatingPointCompleter::Sync& completer) override;
  void SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                             SetDvfsOperatingPointCompleter::Sync& completer) override;
  void GetFanLevel(GetFanLevelCompleter::Sync& completer) override;
  void SetFanLevel(SetFanLevelRequestView request, SetFanLevelCompleter::Sync& completer) override;

  zx_status_t StartConnectDispatchThread();

  std::unique_ptr<thermal::AmlTSensor> tsensor_;
  fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config_;
  async::Loop loop_;
};
}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_THERMAL_H_
