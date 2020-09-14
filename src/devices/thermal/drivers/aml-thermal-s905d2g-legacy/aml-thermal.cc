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

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/reg.h>

namespace thermal {

zx_status_t AmlThermal::SetTarget(uint32_t opp_idx,
                                  fuchsia_hardware_thermal_PowerDomain power_domain) {
  if (opp_idx >= fuchsia_hardware_thermal_MAX_DVFS_OPPS) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Get current settings.
  uint32_t old_voltage = voltage_regulator_->GetVoltage(power_domain);
  uint32_t old_frequency = cpufreq_scaling_->GetFrequency(power_domain);

  // Get new settings.
  uint32_t new_voltage = thermal_config_.opps[power_domain].opp[opp_idx].volt_uv;
  uint32_t new_frequency = thermal_config_.opps[power_domain].opp[opp_idx].freq_hz;

  zxlogf(INFO, "Scaling from %d MHz, %u mV, --> %d MHz, %u mV", old_frequency / 1000000,
         old_voltage / 1000, new_frequency / 1000000, new_voltage / 1000);

  // If new settings are same as old, don't do anything.
  if (new_frequency == old_frequency) {
    return ZX_OK;
  }

  zx_status_t status;
  // Increasing CPU Frequency from current value, so we first change the voltage.
  if (new_frequency > old_frequency) {
    status = voltage_regulator_->SetVoltage(power_domain, new_voltage);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-thermal: Could not change CPU voltage: %d", status);
      return status;
    }
  }

  // Now let's change CPU frequency.
  status = cpufreq_scaling_->SetFrequency(power_domain, new_frequency);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not change CPU frequency: %d", status);
    // Failed to change CPU frequency, change back to old
    // voltage before returning.
    status = voltage_regulator_->SetVoltage(power_domain, old_voltage);
    if (status != ZX_OK) {
      return status;
    }
    return status;
  }

  // Decreasing CPU Frequency from current value, changing voltage after frequency.
  if (new_frequency < old_frequency) {
    status = voltage_regulator_->SetVoltage(power_domain, new_voltage);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-thermal: Could not change CPU voltage: %d", status);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t AmlThermal::Create(void* ctx, zx_device_t* device) {
  ddk::CompositeProtocolClient composite(device);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: failed to get composite protocol", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // zeroth fragment is pdev
  zx_device_t* fragment;
  size_t actual;
  composite.GetFragments(&fragment, 1, &actual);
  if (actual != 1) {
    zxlogf(ERROR, "%s: failed to get pdev fragment", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(fragment);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-thermal: failed to get pdev protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t device_info;
  zx_status_t status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: failed to get GetDeviceInfo ");
    return status;
  }

  // Get the voltage-table .
  aml_thermal_info_t thermal_info;
  status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &thermal_info, sizeof(thermal_info),
                               &actual);
  if (status != ZX_OK || actual != sizeof(thermal_info)) {
    zxlogf(ERROR, "aml-thermal: Could not get voltage-table metadata %d", status);
    return status;
  }

  // Get the thermal policy metadata.
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config;
  status = device_get_metadata(device, DEVICE_METADATA_THERMAL_CONFIG, &thermal_config,
                               sizeof(fuchsia_hardware_thermal_ThermalDeviceInfo), &actual);
  if (status != ZX_OK || actual != sizeof(fuchsia_hardware_thermal_ThermalDeviceInfo)) {
    zxlogf(ERROR, "aml-thermal: Could not get thermal config metadata %d", status);
    return status;
  }

  zx::resource smc_resource;
  pdev.GetSmc(0, &smc_resource);
  status = PopulateDvfsTable(smc_resource, thermal_info, &thermal_config);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto tsensor = fbl::make_unique_checked<AmlTSensor>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize Temperature Sensor.
  status = tsensor->Create(fragment, thermal_config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not initialize Temperature Sensor: %d", status);
    return status;
  }

  // Create the voltage regulator.
  auto voltage_regulator = fbl::make_unique_checked<AmlVoltageRegulator>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize voltage regulator.
  status = voltage_regulator->Create(device, thermal_config, &thermal_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not initialize Voltage Regulator: %d", status);
    return status;
  }

  // Create the CPU frequency scaling object.
  auto cpufreq_scaling = fbl::make_unique_checked<AmlCpuFrequency>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize CPU frequency scaling.
  status = cpufreq_scaling->Create(device, thermal_config, thermal_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not initialize CPU freq. scaling: %d", status);
    return status;
  }

  auto thermal_device = fbl::make_unique_checked<AmlThermal>(
      &ac, device, std::move(tsensor), std::move(voltage_regulator), std::move(cpufreq_scaling),
      std::move(thermal_config));
  if (!ac.check()) {
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

  // Set the default CPU frequency.
  // We could be running Zircon only, or thermal daemon might not
  // run, so we manually set the CPU frequency here.
  uint32_t big_opp_idx = thermal_device->thermal_config_.trip_point_info[0].big_cluster_dvfs_opp;
  status = thermal_device->SetTarget(big_opp_idx,
                                     fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  if (status != ZX_OK) {
    return status;
  }

  if (thermal_config.big_little) {
    uint32_t little_opp_idx =
        thermal_device->thermal_config_.trip_point_info[0].little_cluster_dvfs_opp;
    status = thermal_device->SetTarget(
        little_opp_idx, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN);
    if (status != ZX_OK) {
      return status;
    }
  }

  // devmgr is now in charge of the memory for dev.
  __UNUSED auto ptr = thermal_device.release();
  return ZX_OK;
}

zx_status_t AmlThermal::StartConnectDispatchThread() { return loop_.StartThread(); }

zx_status_t AmlThermal::ThermalConnect(zx::channel chan) {
  zx_status_t st =
      fidl_bind(loop_.dispatcher(), chan.release(),
                reinterpret_cast<fidl_dispatch_t*>(fuchsia_hardware_thermal_Device_dispatch), this,
                &fidl_ops);

  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to start FIDL dispatcher, st = %d", st);
  }

  return st;
}

zx_status_t AmlThermal::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t AmlThermal::GetInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
}

zx_status_t AmlThermal::GetDeviceInfo(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDeviceInfo_reply(txn, ZX_OK, &thermal_config_);
}

zx_status_t AmlThermal::GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
  scpi_opp_t opps = {};
  opps = thermal_config_.opps[power_domain];
  return fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_OK, &opps);
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
  zx_handle_t handle;
  zx_status_t status = tsensor_->GetStateChangePort(&handle);
  return fuchsia_hardware_thermal_DeviceGetStateChangePort_reply(txn, status, handle);
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
  return fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(
      txn, SetTarget(op_idx, power_domain));
}

zx_status_t AmlThermal::GetFanLevel(fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

zx_status_t AmlThermal::SetFanLevel(uint32_t fan_level, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlThermal::DdkRelease() { delete this; }

zx_status_t AmlThermal::PopulateClusterDvfsTable(
    const zx::resource& smc_resource, const aml_thermal_info_t& aml_info,
    fuchsia_hardware_thermal_PowerDomain cluster,
    fuchsia_hardware_thermal_ThermalDeviceInfo* thermal_info) {
  zx_smc_parameters_t smc_params = {};
  smc_params.func_id = AMLOGIC_SMC_GET_DVFS_TABLE_INDEX;
  smc_params.arg1 = aml_info.cluster_id_map[cluster];

  zx_smc_result_t smc_result;
  zx_status_t status = zx_smc_call(smc_resource.get(), &smc_params, &smc_result);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: zx_smc_call failed: %d", status);
    return status;
  }

  if (smc_result.arg0 >= std::size(aml_info.opps[0])) {
    zxlogf(ERROR, "aml-thermal: DVFS table index out of range: %lu", smc_result.arg0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  thermal_info->opps[cluster] = aml_info.opps[cluster][smc_result.arg0];

  return ZX_OK;
}

zx_status_t AmlThermal::PopulateDvfsTable(
    const zx::resource& smc_resource, const aml_thermal_info_t& aml_info,
    fuchsia_hardware_thermal_ThermalDeviceInfo* thermal_info) {
  if (!smc_resource.is_valid()) {
    // No SMC resource was specified, so expect the operating points to be in ThermalDeviceInfo.
    return ZX_OK;
  }

  zx_status_t status = PopulateClusterDvfsTable(
      smc_resource, aml_info, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN,
      thermal_info);

  if (status == ZX_OK && thermal_info->big_little) {
    status = PopulateClusterDvfsTable(
        smc_resource, aml_info, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
        thermal_info);
  }

  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlThermal::Create;
  return ops;
}();

}  // namespace thermal

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_thermal, thermal::driver_ops, "aml-therm-lgcy", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_THERMAL_PLL),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D3),
ZIRCON_DRIVER_END(aml_thermal)
