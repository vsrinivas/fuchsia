// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-thermal.h"

#include <lib/device-protocol/pdev.h>
#include <lib/zx/time.h>

#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

#include "as370-thermal-reg.h"
#include "src/devices/thermal/drivers/as370-thermal/as370-thermal-bind.h"

namespace {

constexpr uint32_t kEocLoopTimeout = 20000;
constexpr zx::duration kEocLoopSleepTime = zx::usec(100);

constexpr float SensorReadingToTemperature(int32_t reading) {
  reading = reading * 251802 / 4096 - 85525;
  return static_cast<float>(reading) / 1000.0f;
}

}  // namespace

namespace thermal {

using llcpp::fuchsia::hardware::thermal::OperatingPoint;

zx_status_t As370Thermal::Create(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get composite protocol", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PDev pdev(composite);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get platform device protocol", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::ClockProtocolClient cpu_clock(composite, "clock");
  if (!cpu_clock.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get clock protocol", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PowerProtocolClient cpu_power(composite, "power");
  if (!cpu_power.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get power protocol", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio;
  zx_status_t status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map MMIO: %d", __func__, status);
    return status;
  }

  size_t actual_size = 0;
  ThermalDeviceInfo device_info = {};
  if ((status = device_get_metadata(parent, DEVICE_METADATA_THERMAL_CONFIG, &device_info,
                                    sizeof(device_info), &actual_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get metadata: %d", __func__, status);
    return status;
  }
  if (actual_size != sizeof(device_info)) {
    zxlogf(ERROR, "%s: Metadata size mismatch", __func__);
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<As370Thermal>(&ac, parent, *std::move(mmio), device_info,
                                                       cpu_clock, cpu_power);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate device memory", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("as370-thermal")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

zx_status_t As370Thermal::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::thermal::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void As370Thermal::GetInfo(GetInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void As370Thermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  ThermalDeviceInfo device_info_copy = device_info_;
  completer.Reply(ZX_OK, fidl::unowned_ptr(&device_info_copy));
}

void As370Thermal::GetDvfsInfo(PowerDomain power_domain, GetDvfsInfoCompleter::Sync& completer) {
  if (power_domain != PowerDomain::BIG_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
  } else {
    OperatingPoint dvfs_info_copy = device_info_.opps[static_cast<uint32_t>(power_domain)];
    completer.Reply(ZX_OK, fidl::unowned_ptr(&dvfs_info_copy));
  }
}

void As370Thermal::GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) {
  PvtCtrl::Get()
      .ReadFrom(&mmio_)
      .set_pmos_sel(0)
      .set_nmos_sel(0)
      .set_voltage_sel(0)
      .set_temperature_sel(1)
      .WriteTo(&mmio_)
      .set_enable(1)
      .WriteTo(&mmio_)
      .set_power_down(0)
      .WriteTo(&mmio_);

  auto pvt_status = PvtStatus::Get().FromValue(0);
  for (uint32_t i = 0; i < kEocLoopTimeout && pvt_status.ReadFrom(&mmio_).eoc() == 0; i++) {
    zx::nanosleep(zx::deadline_after(kEocLoopSleepTime));
  }

  PvtCtrl::Get().FromValue(0).set_power_down(1).WriteTo(&mmio_);
  if (pvt_status.eoc() == 0) {
    zxlogf(ERROR, "%s: Timed out waiting for temperature reading", __func__);
    completer.Reply(ZX_ERR_TIMED_OUT, 0.0f);
  } else {
    completer.Reply(ZX_OK, SensorReadingToTemperature(pvt_status.data()));
  }
}

void As370Thermal::GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void As370Thermal::GetStateChangePort(GetStateChangePortCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void As370Thermal::SetTripCelsius(uint32_t id, float temp,
                                  SetTripCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void As370Thermal::GetDvfsOperatingPoint(PowerDomain power_domain,
                                         GetDvfsOperatingPointCompleter::Sync& completer) {
  if (power_domain != PowerDomain::BIG_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
  } else {
    completer.Reply(ZX_OK, operating_point_);
  }
}

void As370Thermal::SetDvfsOperatingPoint(uint16_t op_idx, PowerDomain power_domain,
                                         SetDvfsOperatingPointCompleter::Sync& completer) {
  if (power_domain != PowerDomain::BIG_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  } else if (op_idx >= device_info_.opps[static_cast<uint32_t>(power_domain)].count) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
  } else {
    completer.Reply(SetOperatingPoint(op_idx));
  }
}

void As370Thermal::GetFanLevel(GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void As370Thermal::SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t As370Thermal::Init() {
  PvtCtrl::Get().FromValue(0).set_power_down(1).WriteTo(&mmio_);

  const OperatingPoint& operating_points =
      device_info_.opps[static_cast<uint32_t>(PowerDomain::BIG_CLUSTER_POWER_DOMAIN)];
  const auto max_operating_point = static_cast<uint16_t>(operating_points.count - 1);

  zx_status_t status = cpu_power_.RegisterPowerDomain(
      operating_points.opp[0].volt_uv, operating_points.opp[max_operating_point].volt_uv);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to register power domain: %d", __func__, status);
    return status;
  }

  return SetOperatingPoint(max_operating_point);
}

zx_status_t As370Thermal::SetOperatingPoint(uint16_t op_idx) {
  const auto& opps =
      device_info_.opps[static_cast<uint32_t>(PowerDomain::BIG_CLUSTER_POWER_DOMAIN)].opp;

  zx_status_t status;
  uint32_t actual_voltage = 0;
  if (opps[op_idx].freq_hz > opps[operating_point_].freq_hz) {
    if ((status = cpu_power_.RequestVoltage(opps[op_idx].volt_uv, &actual_voltage)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set voltage: %d", __func__, status);
      return status;
    }
    if (actual_voltage != opps[op_idx].volt_uv) {
      zxlogf(ERROR, "%s: Failed to set exact voltage: set %u, wanted %u", __func__, actual_voltage,
             opps[op_idx].volt_uv);
      return ZX_ERR_BAD_STATE;
    }

    if ((status = cpu_clock_.SetRate(opps[op_idx].freq_hz)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set CPU frequency: %d", __func__, status);
      return status;
    }
  } else {
    if ((status = cpu_clock_.SetRate(opps[op_idx].freq_hz)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set CPU frequency: %d", __func__, status);
      return status;
    }

    if ((status = cpu_power_.RequestVoltage(opps[op_idx].volt_uv, &actual_voltage)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set voltage: %d", __func__, status);
      return status;
    }
    if (actual_voltage != opps[op_idx].volt_uv) {
      zxlogf(ERROR, "%s: Failed to set exact voltage: set %u, wanted %u", __func__, actual_voltage,
             opps[op_idx].volt_uv);
      return ZX_ERR_BAD_STATE;
    }
  }

  operating_point_ = op_idx;
  return ZX_OK;
}

}  // namespace thermal

static constexpr zx_driver_ops_t as370_thermal_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = thermal::As370Thermal::Create;
  return ops;
}();

ZIRCON_DRIVER(as370_thermal, as370_thermal_driver_ops, "zircon", "0.1");
