// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/device-protocol/pdev.h>
#include <string.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

#include <utility>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/reg.h>

#include "src/devices/thermal/drivers/aml-thermal-s905d2g/aml-thermal-s905d2g-bind.h"

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
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config;
  status = device_get_metadata(device, DEVICE_METADATA_THERMAL_CONFIG, &thermal_config,
                               sizeof(fuchsia_hardware_thermal_ThermalDeviceInfo), &actual);
  if (status != ZX_OK || actual != sizeof(fuchsia_hardware_thermal_ThermalDeviceInfo)) {
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

zx_status_t AmlThermal::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t AmlThermal::GetInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
}

zx_status_t AmlThermal::GetDeviceInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDeviceInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
}

zx_status_t AmlThermal::GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
}

zx_status_t AmlThermal::GetTemperatureCelsius(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetTemperatureCelsius_reply(
      txn, ZX_OK, tsensor_->ReadTemperatureCelsius());
}

zx_status_t AmlThermal::GetStateChangeEvent(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetStateChangeEvent_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                  ZX_HANDLE_INVALID);
}

zx_status_t AmlThermal::GetStateChangePort(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetStateChangePort_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                 ZX_HANDLE_INVALID);
}

zx_status_t AmlThermal::SetTripCelsius(uint32_t id, float temp, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetTripCelsius_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t AmlThermal::GetDvfsOperatingPoint(fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t AmlThermal::SetDvfsOperatingPoint(uint16_t op_idx,
                                              fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t AmlThermal::GetFanLevel(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t AmlThermal::SetFanLevel(uint32_t fan_level, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t AmlThermal::ThermalConnect(zx::channel ch) { return ZX_ERR_NOT_SUPPORTED; }

void AmlThermal::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlThermal::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlThermal::Create;
  return ops;
}();

}  // namespace thermal

ZIRCON_DRIVER(aml_thermal, thermal::driver_ops, "aml-thermal", "0.1");
