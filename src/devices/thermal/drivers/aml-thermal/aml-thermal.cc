// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <string.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/thermal/drivers/aml-thermal/aml-thermal-bind.h"

namespace thermal {

zx_status_t AmlThermal::Create(void* ctx, zx_device_t* device) {
  ddk::PDevProtocolClient pdev(device);
  zx_status_t status;
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev protocol", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Get the thermal policy metadata.
  size_t actual;
  fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config;
  status = device_get_metadata(device, DEVICE_METADATA_THERMAL_CONFIG, &thermal_config,
                               sizeof(fuchsia_hardware_thermal::wire::ThermalDeviceInfo), &actual);
  if (status != ZX_OK || actual != sizeof(fuchsia_hardware_thermal::wire::ThermalDeviceInfo)) {
    zxlogf(ERROR, "aml-thermal: Could not get thermal config metadata %d", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto tsensor = fbl::make_unique_checked<AmlTSensor>(&ac);
  if (!ac.check()) {
    zxlogf(ERROR, "aml-thermal; Failed to allocate AmlTSensor");
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize Temperature Sensor.
  status = tsensor->Create(device, thermal_config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not initialize Temperature Sensor: %d", status);
    return status;
  }

  auto thermal_device = fbl::make_unique_checked<AmlThermal>(&ac, device, std::move(tsensor),
                                                             std::move(thermal_config));
  if (!ac.check()) {
    zxlogf(ERROR, "aml-thermal; Failed to allocate AmlThermal");
    return ZX_ERR_NO_MEMORY;
  }

  status = thermal_device->StartConnectDispatchThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not start connect dispatcher thread, st = %d", status);
    return status;
  }

  status = thermal_device->DdkAdd(ddk::DeviceAddArgs("thermal").set_proto_id(ZX_PROTOCOL_THERMAL));
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not create thermal device: %d", status);
    return status;
  }

  // devmgr is now in charge of the memory for dev.
  __UNUSED auto ptr = thermal_device.release();
  return ZX_OK;
}

zx_status_t AmlThermal::StartConnectDispatchThread() { return loop_.StartThread(); }

void AmlThermal::GetInfo(GetInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void AmlThermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void AmlThermal::GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_OK, tsensor_->ReadTemperatureCelsius());
}

void AmlThermal::GetDvfsInfo(GetDvfsInfoRequestView request,
                             GetDvfsInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void AmlThermal::GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::event());
}

void AmlThermal::GetStateChangePort(GetStateChangePortCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::port());
}

void AmlThermal::SetTripCelsius(SetTripCelsiusRequestView request,
                                SetTripCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                                       GetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void AmlThermal::SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                                       SetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::GetFanLevel(GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void AmlThermal::SetFanLevel(SetFanLevelRequestView request,
                             SetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t AmlThermal::ThermalConnect(zx::channel ch) { return ZX_ERR_NOT_SUPPORTED; }

void AmlThermal::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlThermal::Create;
  return ops;
}();

}  // namespace thermal

ZIRCON_DRIVER(aml_thermal, thermal::driver_ops, "aml-thermal", "0.1");
