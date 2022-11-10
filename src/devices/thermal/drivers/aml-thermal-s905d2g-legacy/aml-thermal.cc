// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
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

#include "lib/fidl/cpp/wire/object_view.h"
#include "src/devices/thermal/drivers/aml-thermal-s905d2g-legacy/aml-thermal-bind.h"

namespace thermal {

zx_status_t AmlThermal::SetTarget(uint32_t opp_idx,
                                  fuchsia_hardware_thermal::wire::PowerDomain power_domain) {
  if (opp_idx >= fuchsia_hardware_thermal::wire::kMaxDvfsOpps) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Get current settings.
  uint32_t old_voltage = voltage_regulator_->GetVoltage(power_domain);
  uint32_t old_frequency = cpufreq_scaling_->GetFrequency(power_domain);

  // Get new settings.
  uint32_t new_voltage =
      thermal_config_.opps[static_cast<uint32_t>(power_domain)].opp[opp_idx].volt_uv;
  uint32_t new_frequency =
      thermal_config_.opps[static_cast<uint32_t>(power_domain)].opp[opp_idx].freq_hz;

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
  auto pdev = ddk::PDev::FromFragment(device);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-thermal: failed to get pdev protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t device_info;
  zx_status_t status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: failed to get device info: %d", status);
    return status;
  }

  // Get the voltage-table .
  size_t actual;
  aml_thermal_info_t thermal_info;
  status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &thermal_info, sizeof(thermal_info),
                               &actual);
  if (status != ZX_OK || actual != sizeof(thermal_info)) {
    zxlogf(ERROR, "aml-thermal: Could not get voltage-table metadata %d", status);
    return status;
  }

  // Get the thermal policy metadata.
  fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config;
  status = device_get_metadata(device, DEVICE_METADATA_THERMAL_CONFIG, &thermal_config,
                               sizeof(fuchsia_hardware_thermal::wire::ThermalDeviceInfo), &actual);
  if (status != ZX_OK || actual != sizeof(fuchsia_hardware_thermal::wire::ThermalDeviceInfo)) {
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
  status = tsensor->Create(device, thermal_config);
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
      thermal_config);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = thermal_device->StartConnectDispatchThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not start connect dispatcher thread, st = %d", status);
    return status;
  }

  zx_device_prop_t props[] = {
      {.id = BIND_PLATFORM_DEV_DID, .reserved = 0, .value = device_info.did}};
  status = thermal_device->DdkAdd(
      ddk::DeviceAddArgs("thermal").set_props(props).set_proto_id(ZX_PROTOCOL_THERMAL));
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-thermal: Could not create thermal device: %d", status);
    return status;
  }

  // Set the default CPU frequency.
  // We could be running Zircon only, or thermal daemon might not
  // run, so we manually set the CPU frequency here.
  uint32_t big_opp_idx = thermal_device->thermal_config_.trip_point_info[0].big_cluster_dvfs_opp;
  status = thermal_device->SetTarget(
      big_opp_idx, fuchsia_hardware_thermal::wire::PowerDomain::kBigClusterPowerDomain);
  if (status != ZX_OK) {
    return status;
  }

  if (thermal_config.big_little) {
    uint32_t little_opp_idx =
        thermal_device->thermal_config_.trip_point_info[0].little_cluster_dvfs_opp;
    status = thermal_device->SetTarget(
        little_opp_idx, fuchsia_hardware_thermal::wire::PowerDomain::kLittleClusterPowerDomain);
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
  fidl::BindServer(loop_.dispatcher(),
                   fidl::ServerEnd<fuchsia_hardware_thermal::Device>(std::move(chan)), this);
  return ZX_OK;
}

void AmlThermal::GetInfo(GetInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AmlThermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  completer.Reply(ZX_OK,
                  fidl::ObjectView<decltype(thermal_config_)>::FromExternal(&thermal_config_));
}

void AmlThermal::GetDvfsInfo(GetDvfsInfoRequestView request,
                             GetDvfsInfoCompleter::Sync& completer) {
  fuchsia_hardware_thermal::wire::OperatingPoint opp =
      thermal_config_.opps[static_cast<uint32_t>(request->power_domain)];
  completer.Reply(ZX_OK, fidl::ObjectView<decltype(opp)>::FromExternal(&opp));
}

void AmlThermal::GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_OK, tsensor_->ReadTemperatureCelsius());
}

void AmlThermal::GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AmlThermal::GetStateChangePort(GetStateChangePortCompleter::Sync& completer) {
  zx::port port;
  zx_status_t status = tsensor_->GetStateChangePort(port.reset_and_get_address());
  completer.Reply(status, std::move(port));
}

void AmlThermal::SetTripCelsius(SetTripCelsiusRequestView request,
                                SetTripCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                                       GetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AmlThermal::SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                                       SetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(SetTarget(request->op_idx, request->power_domain));
}

void AmlThermal::GetFanLevel(GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AmlThermal::SetFanLevel(SetFanLevelRequestView request,
                             SetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::DdkRelease() { delete this; }

zx_status_t AmlThermal::PopulateClusterDvfsTable(
    const zx::resource& smc_resource, const aml_thermal_info_t& aml_info,
    fuchsia_hardware_thermal::wire::PowerDomain cluster,
    fuchsia_hardware_thermal::wire::ThermalDeviceInfo* thermal_info) {
  zx_smc_parameters_t smc_params = {};
  smc_params.func_id = AMLOGIC_SMC_GET_DVFS_TABLE_INDEX;
  smc_params.arg1 = aml_info.cluster_id_map[static_cast<uint32_t>(cluster)];

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

  thermal_info->opps[static_cast<uint32_t>(cluster)] =
      aml_info.opps[static_cast<uint32_t>(cluster)][smc_result.arg0];

  return ZX_OK;
}

zx_status_t AmlThermal::PopulateDvfsTable(
    const zx::resource& smc_resource, const aml_thermal_info_t& aml_info,
    fuchsia_hardware_thermal::wire::ThermalDeviceInfo* thermal_info) {
  if (!smc_resource.is_valid()) {
    // No SMC resource was specified, so expect the operating points to be in ThermalDeviceInfo.
    return ZX_OK;
  }

  zx_status_t status = PopulateClusterDvfsTable(
      smc_resource, aml_info, fuchsia_hardware_thermal::wire::PowerDomain::kBigClusterPowerDomain,
      thermal_info);

  if (status == ZX_OK && thermal_info->big_little) {
    status = PopulateClusterDvfsTable(
        smc_resource, aml_info,
        fuchsia_hardware_thermal::wire::PowerDomain::kLittleClusterPowerDomain, thermal_info);
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

ZIRCON_DRIVER(aml_thermal, thermal::driver_ops, "aml-therm-lgcy", "0.1");
