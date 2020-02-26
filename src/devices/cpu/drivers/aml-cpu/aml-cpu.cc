// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/thermal.h>

namespace {
using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;
using llcpp::fuchsia::hardware::thermal::PowerDomain;

__UNUSED constexpr size_t kComponentPdev = 0;
constexpr size_t kComponentThermal = 1;
constexpr size_t kComponentCount = 2;

uint16_t PstateToOperatingPoint(const uint32_t pstate, const size_t n_operating_points) {
  ZX_ASSERT(pstate < n_operating_points);
  ZX_ASSERT(n_operating_points < MAX_DEVICE_PERFORMANCE_STATES);

  // Operating points are indexed 0 to N-1.
  return static_cast<uint16_t>(n_operating_points - pstate - 1);
}

}  // namespace
namespace amlogic_cpu {

zx_status_t AmlCpu::Create(void* context, zx_device_t* parent) {
  zx_status_t status;

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: failed to get composite protocol\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx_device_t* devices[kComponentCount];
  size_t actual;
  composite.GetComponents(devices, kComponentCount, &actual);
  if (actual != kComponentCount) {
    zxlogf(ERROR, "%s: Expected to get %lu components, actually got %lu\n", __func__,
           kComponentCount, actual);
    return ZX_ERR_INTERNAL;
  }

  // Initialize an array with the maximum possible number of PStates since we
  // determine the actual number of PStates at runtime by querying the thermal
  // driver.
  device_performance_state_info_t perf_states[MAX_DEVICE_PERFORMANCE_STATES];
  for (size_t i = 0; i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
    perf_states[i].state_id = static_cast<uint8_t>(i);
    perf_states[i].restore_latency = 0;
  }

  // The Thermal Driver is our parent and it exports an interface with one
  // method (Connect) which allows us to connect to its FIDL interface.
  zx_device_t* device = devices[kComponentThermal];
  ddk::ThermalProtocolClient thermal_client;
  status = ddk::ThermalProtocolClient::CreateFromDevice(device, &thermal_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to get thermal protocol client, st = %d\n", status);
    return status;
  }

  // This channel pair will be used to talk to the Thermal Device's FIDL
  // interface.
  zx::channel channel_local, channel_remote;
  status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to create channel pair, st = %d\n", status);
    return status;
  }

  // Pass one end of the channel to the Thermal driver. The thermal driver will
  // serve its FIDL interface over this channel.
  status = thermal_client.Connect(std::move(channel_remote));
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: failed to connect to thermal driver, st = %d\n", status);
    return status;
  }

  fuchsia_thermal::Device::SyncClient thermal_fidl_client(std::move(channel_local));

  auto device_info = thermal_fidl_client.GetDeviceInfo();
  if (device_info.status() != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: failed to get device info, st = %d\n", device_info.status());
    return device_info.status();
  }

  const fuchsia_thermal::ThermalDeviceInfo* info = device_info->info.get();

  // Hack: Only support one DVFS domain in this driver. When only one domain is
  // supported, it is published as the "Big" domain, so we check that the Little
  // domain is unpopulated.
  constexpr size_t kLittleDomainIndex = 1u;
  static_assert(static_cast<size_t>(PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN) ==
                kLittleDomainIndex);
  if (info->opps[kLittleDomainIndex].count != 0) {
    zxlogf(ERROR, "aml-cpu: this driver only supports one dvfs domain.\n");
    return ZX_ERR_INTERNAL;
  }

  // Make sure we don't have more operating points than available performance states.
  const fuchsia_thermal::OperatingPoint& opps = info->opps[0];
  if (opps.count > MAX_DEVICE_PERFORMANCE_STATES) {
    zxlogf(ERROR, "aml-cpu: cpu device has more operating points than we support\n");
    return ZX_ERR_INTERNAL;
  }

  const uint8_t perf_state_count = static_cast<uint8_t>(opps.count);
  zxlogf(INFO, "aml-cpu: Creating CPU Device with %u operating poitns\n", opps.count);

  auto cpu_device = std::make_unique<AmlCpu>(device, std::move(thermal_fidl_client));

  status = cpu_device->DdkAdd("cpu",                         // name
                              DEVICE_ADD_NON_BINDABLE,       // flags
                              nullptr, 0,                    // props & propcount
                              ZX_PROTOCOL_CPU_CTRL,          // protocol id
                              nullptr,                       // proxy_args
                              ZX_HANDLE_INVALID,             // client remote
                              nullptr, 0,                    // Power states & count
                              perf_states, perf_state_count  // Perf states & count
  );
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to add cpu device, st = %d\n", status);
    return status;
  }

  // Intentionally leak this device because it's owned by the driver framework.
  __UNUSED auto unused = cpu_device.release();

  return ZX_OK;
}

zx_status_t AmlCpu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_cpuctrl::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlCpu::DdkRelease() { delete this; }

zx_status_t AmlCpu::DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state) {
  zx_status_t status;
  fuchsia_thermal::OperatingPoint opps;

  status = GetThermalOperatingPoints(&opps);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Thermal operating poitns, st = %d\n", __func__, status);
    return status;
  }

  if (requested_state >= opps.count) {
    zxlogf(ERROR, "%s: Requested device performance state is out of bounds\n", __func__);
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint16_t pstate = PstateToOperatingPoint(requested_state, opps.count);

  const auto result =
      thermal_client_.SetDvfsOperatingPoint(pstate, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);

  if (!result.ok() || result->status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to set dvfs operating point.\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  *out_state = requested_state;
  return ZX_OK;
}

zx_status_t AmlCpu::DdkConfigureAutoSuspend(bool enable, uint8_t requested_sleep_state) {
  return ZX_ERR_NOT_SUPPORTED;
}

void AmlCpu::GetPerformanceStateInfo(uint32_t state,
                                     GetPerformanceStateInfoCompleter::Sync completer) {
  // Get all performance states.
  zx_status_t status;
  fuchsia_thermal::OperatingPoint opps;

  status = GetThermalOperatingPoints(&opps);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Thermal operating poitns, st = %d\n", __func__, status);
    completer.ReplyError(status);
  }

  // Make sure that the state is in bounds?
  if (state >= opps.count) {
    zxlogf(ERROR, "%s: requested pstate index out of bounds, requested = %u, count = %u\n",
           __func__, state, opps.count);
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  const uint16_t pstate = PstateToOperatingPoint(state, opps.count);

  llcpp::fuchsia::hardware::cpu::ctrl::CpuPerformanceStateInfo result;
  result.frequency_hz = opps.opp[pstate].freq_hz;
  result.voltage_uv = opps.opp[pstate].volt_uv;
  completer.ReplySuccess(result);
}

zx_status_t AmlCpu::GetThermalOperatingPoints(fuchsia_thermal::OperatingPoint* out) {
  auto result = thermal_client_.GetDeviceInfo();
  if (!result.ok() || result->status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get thermal device info\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  fuchsia_thermal::ThermalDeviceInfo* info = result->info.get();

  // We only support one DVFS cluster on Astro.
  if (info->opps[1].count != 0) {
    zxlogf(ERROR, "%s: thermal driver reported more than one dvfs domain?\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  memcpy(out, &info->opps[0], sizeof(*out));
  return ZX_OK;
}

void AmlCpu::GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync completer) {
  unsigned int result = zx_system_get_num_cpus();
  completer.Reply(result);
}

void AmlCpu::GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync completer) {
  // Placeholder.
  completer.Reply(0);
}

}  // namespace amlogic_cpu

static constexpr zx_driver_ops_t aml_cpu_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_cpu::AmlCpu::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_cpu, aml_cpu_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CPU),
ZIRCON_DRIVER_END(aml_cpu)
