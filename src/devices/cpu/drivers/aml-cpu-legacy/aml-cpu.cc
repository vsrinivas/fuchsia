// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/thermal/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/mmio/mmio.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <optional>

#include <ddktl/fidl.h>
#include <soc/aml-common/aml-cpu-metadata.h>

#include "fidl/fuchsia.hardware.thermal/cpp/wire.h"
#include "src/devices/cpu/drivers/aml-cpu-legacy/aml-cpu-legacy-bind.h"

namespace {
using fuchsia_device::wire::kMaxDevicePerformanceStates;
using fuchsia_hardware_thermal::wire::kMaxDvfsDomains;
using fuchsia_hardware_thermal::wire::PowerDomain;

constexpr zx_off_t kCpuVersionOffset = 0x220;

uint16_t PstateToOperatingPoint(const uint32_t pstate, const size_t n_operating_points) {
  ZX_ASSERT(pstate < n_operating_points);
  ZX_ASSERT(n_operating_points < kMaxDevicePerformanceStates);

  // Operating points are indexed 0 to N-1.
  return static_cast<uint16_t>(n_operating_points - pstate - 1);
}

fidl::WireSyncClient<amlogic_cpu::fuchsia_thermal::Device> CreateFidlClient(
    const ddk::ThermalProtocolClient& protocol_client, zx_status_t* status) {
  // This channel pair will be used to talk to the Thermal Device's FIDL
  // interface.
  zx::channel channel_local, channel_remote;
  *status = zx::channel::create(0, &channel_local, &channel_remote);
  if (*status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to create channel pair, st = %d\n", *status);
    return {};
  }

  // Pass one end of the channel to the Thermal driver. The thermal driver will
  // serve its FIDL interface over this channel.
  *status = protocol_client.Connect(std::move(channel_remote));
  if (*status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: failed to connect to thermal driver, st = %d\n", *status);
    return {};
  }

  return fidl::WireSyncClient<amlogic_cpu::fuchsia_thermal::Device>(std::move(channel_local));
}

zx_status_t GetDeviceName(bool big_little, PowerDomain power_domain, char const** name) {
  if (!big_little) {
    *name = "domain-0";
  } else {
    switch (power_domain) {
      case PowerDomain::kBigClusterPowerDomain:
        *name = "big-cluster";
        break;
      case PowerDomain::kLittleClusterPowerDomain:
        *name = "little-cluster";
        break;
      default:
        zxlogf(ERROR, "aml-cpu: Got invalid power domain %u", static_cast<uint32_t>(power_domain));
        *name = "invalid";
        return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

}  // namespace
namespace amlogic_cpu {

zx_status_t AmlCpu::Create(void* context, zx_device_t* parent) {
  zx_status_t status;
  // Initialize an array with the maximum possible number of PStates since we
  // determine the actual number of PStates at runtime by querying the thermal
  // driver.
  device_performance_state_info_t perf_states[kMaxDevicePerformanceStates];
  for (size_t i = 0; i < kMaxDevicePerformanceStates; i++) {
    perf_states[i].state_id = static_cast<uint8_t>(i);
    perf_states[i].restore_latency = 0;
  }

  // Determine the cluster size of each cluster.
  size_t cluster_count_size = 0;
  status =
      device_get_metadata_size(parent, DEVICE_METADATA_CLUSTER_SIZE_LEGACY, &cluster_count_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get metadata DEVICE_METADATA_CLUSTER_SIZE size. st = %d", __func__,
           status);
    return status;
  }

  if (cluster_count_size % sizeof(legacy_cluster_size_t) != 0) {
    zxlogf(ERROR, "%s: Cluster size metadata from board driver is malformed", __func__);
    return ZX_ERR_INTERNAL;
  }

  size_t actual;
  const size_t num_cluster_count_entries = cluster_count_size / sizeof(legacy_cluster_size_t);
  std::unique_ptr<legacy_cluster_size_t[]> cluster_sizes =
      std::make_unique<legacy_cluster_size_t[]>(num_cluster_count_entries);
  status = device_get_metadata(parent, DEVICE_METADATA_CLUSTER_SIZE_LEGACY, cluster_sizes.get(),
                               cluster_count_size, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get cluster size metadata from board driver, st = %d", __func__,
           status);
    return status;
  }
  if (actual != cluster_count_size) {
    zxlogf(ERROR, "%s: Expected %lu bytes in cluster size metadata, got %lu", __func__,
           cluster_count_size, actual);
    return ZX_ERR_INTERNAL;
  }

  std::map<PerfDomainId, uint32_t> cluster_core_counts;
  for (size_t i = 0; i < num_cluster_count_entries; i++) {
    cluster_core_counts[cluster_sizes[i].pd_id] = cluster_sizes[i].core_count;
  }

  // The Thermal Driver is our parent and it exports an interface with one
  // method (Connect) which allows us to connect to its FIDL interface.
  ddk::ThermalProtocolClient thermal_protocol_client;
  status =
      ddk::ThermalProtocolClient::CreateFromDevice(parent, "thermal", &thermal_protocol_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to get thermal protocol client, st = %d", status);
    return status;
  }

  auto thermal_fidl_client = CreateFidlClient(thermal_protocol_client, &status);
  if (!thermal_fidl_client) {
    return status;
  }

  auto device_info = thermal_fidl_client->GetDeviceInfo();
  if (device_info.status() != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: failed to get device info, st = %d", device_info.status());
    return device_info.status();
  }

  const fuchsia_thermal::wire::ThermalDeviceInfo* info = device_info->info.get();

  // Ensure there is at least one non-empty power domain. We expect one to exist if this function
  // has been called.
  {
    bool found_nonempty_domain = false;
    for (size_t i = 0; i < kMaxDvfsDomains; i++) {
      if (info->opps[i].count > 0) {
        found_nonempty_domain = true;
        break;
      }
    }
    if (!found_nonempty_domain) {
      zxlogf(ERROR, "aml-cpu: No cpu devices were created; all power domains are empty\n");
      return ZX_ERR_INTERNAL;
    }
  }

  // Look up the CPU version.
  uint32_t cpu_version_packed = 0;
  {
    ddk::PDev pdev_client = ddk::PDev::FromFragment(parent);
    if (!pdev_client.is_valid()) {
      return ZX_ERR_INTERNAL;
    }

    // Map AOBUS registers
    std::optional<ddk::MmioBuffer> mmio_buffer;

    if ((status = pdev_client.MapMmio(0, &mmio_buffer)) != ZX_OK) {
      zxlogf(ERROR, "aml-cpu: Failed to map mmio, st = %d", status);
      return status;
    }

    cpu_version_packed = mmio_buffer->Read32(kCpuVersionOffset);
  }

  // Create an AmlCpu for each power domain with nonempty operating points.
  for (size_t i = 0; i < kMaxDvfsDomains; i++) {
    const fuchsia_thermal::wire::OperatingPoint& opps = info->opps[i];

    // If this domain is empty, don't create a driver.
    if (opps.count == 0) {
      continue;
    }

    if (opps.count > kMaxDevicePerformanceStates) {
      zxlogf(ERROR, "aml-cpu: cpu power domain %zu has more operating points than we support\n", i);
      return ZX_ERR_INTERNAL;
    }

    const auto& cluster_core_count = cluster_core_counts.find(i);
    if (cluster_core_count == cluster_core_counts.end()) {
      zxlogf(ERROR, "aml-cpu: Could not find cluster core count for cluster %lu", i);
      return ZX_ERR_NOT_FOUND;
    }

    const uint8_t perf_state_count = static_cast<uint8_t>(opps.count);
    zxlogf(INFO, "aml-cpu: Creating CPU Device for domain %zu with %u operating points\n", i,
           opps.count);

    // If the FIDL client has been previously consumed, create a new one. Then build the CPU device
    // and consume the FIDL client.
    if (!thermal_fidl_client) {
      thermal_fidl_client = CreateFidlClient(thermal_protocol_client, &status);
      if (!thermal_fidl_client) {
        return status;
      }
    }
    auto cpu_device = std::make_unique<AmlCpu>(parent, std::move(thermal_fidl_client), i,
                                               cluster_core_count->second);
    thermal_fidl_client = {};

    cpu_device->SetCpuInfo(cpu_version_packed);

    char const* name;
    status = GetDeviceName(info->big_little, static_cast<PowerDomain>(i), &name);
    if (status != ZX_OK) {
      return status;
    }

    status = cpu_device->DdkAdd(ddk::DeviceAddArgs(name)
                                    .set_flags(DEVICE_ADD_NON_BINDABLE)
                                    .set_proto_id(ZX_PROTOCOL_CPU_CTRL)
                                    .set_performance_states({perf_states, perf_state_count})
                                    .set_inspect_vmo(cpu_device->inspector_.DuplicateVmo()));

    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpu: Failed to add cpu device for domain %zu, st = %d\n", i, status);
      return status;
    }

    // Intentionally leak this device because it's owned by the driver framework.
    __UNUSED auto unused = cpu_device.release();
  }

  return ZX_OK;
}

void AmlCpu::DdkRelease() { delete this; }

zx_status_t AmlCpu::DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state) {
  zx_status_t status;
  fuchsia_thermal::wire::OperatingPoint opps;

  status = GetThermalOperatingPoints(&opps);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Thermal operating points, st = %d", __func__, status);
    return status;
  }

  if (requested_state >= opps.count) {
    zxlogf(ERROR, "%s: Requested device performance state is out of bounds", __func__);
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint16_t pstate = PstateToOperatingPoint(requested_state, opps.count);

  const auto result =
      thermal_client_->SetDvfsOperatingPoint(pstate, static_cast<PowerDomain>(power_domain_index_));

  if (!result.ok() || result->status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to set dvfs operating point.", __func__);
    return ZX_ERR_INTERNAL;
  }

  *out_state = requested_state;
  return ZX_OK;
}

zx_status_t AmlCpu::DdkConfigureAutoSuspend(bool enable, uint8_t requested_sleep_state) {
  return ZX_ERR_NOT_SUPPORTED;
}

void AmlCpu::GetPerformanceStateInfo(GetPerformanceStateInfoRequestView request,
                                     GetPerformanceStateInfoCompleter::Sync& completer) {
  // Get all performance states.
  zx_status_t status;
  fuchsia_thermal::wire::OperatingPoint opps;

  status = GetThermalOperatingPoints(&opps);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Thermal operating points, st = %d", __func__, status);
    completer.ReplyError(status);
  }

  // Make sure that the state is in bounds?
  if (request->state >= opps.count) {
    zxlogf(ERROR, "%s: requested pstate index out of bounds, requested = %u, count = %u", __func__,
           request->state, opps.count);
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  const uint16_t pstate = PstateToOperatingPoint(request->state, opps.count);

  fuchsia_hardware_cpu_ctrl::wire::CpuPerformanceStateInfo result;
  result.frequency_hz = opps.opp[pstate].freq_hz;
  result.voltage_uv = opps.opp[pstate].volt_uv;
  completer.ReplySuccess(result);
}

zx_status_t AmlCpu::GetThermalOperatingPoints(fuchsia_thermal::wire::OperatingPoint* out) {
  auto result = thermal_client_->GetDeviceInfo();
  if (!result.ok() || result->status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get thermal device info", __func__);
    return ZX_ERR_INTERNAL;
  }

  fuchsia_thermal::wire::ThermalDeviceInfo* info = result->info.get();

  memcpy(out, &info->opps[power_domain_index_], sizeof(*out));
  return ZX_OK;
}

void AmlCpu::GetNumLogicalCores(GetNumLogicalCoresRequestView request,
                                GetNumLogicalCoresCompleter::Sync& completer) {
  completer.Reply(ClusterCoreCount());
}

void AmlCpu::GetLogicalCoreId(GetLogicalCoreIdRequestView request,
                              GetLogicalCoreIdCompleter::Sync& completer) {
  // Placeholder.
  completer.Reply(0);
}

void AmlCpu::SetCpuInfo(uint32_t cpu_version_packed) {
  const uint8_t major_revision = (cpu_version_packed >> 24) & 0xff;
  const uint8_t minor_revision = (cpu_version_packed >> 8) & 0xff;
  const uint8_t cpu_package_id = (cpu_version_packed >> 20) & 0x0f;
  zxlogf(INFO, "major revision number: 0x%x", major_revision);
  zxlogf(INFO, "minor revision number: 0x%x", minor_revision);
  zxlogf(INFO, "cpu package id number: 0x%x", cpu_package_id);

  cpu_info_.CreateUint("cpu_major_revision", major_revision, &inspector_);
  cpu_info_.CreateUint("cpu_minor_revision", minor_revision, &inspector_);
  cpu_info_.CreateUint("cpu_package_id", cpu_package_id, &inspector_);
}

}  // namespace amlogic_cpu

static constexpr zx_driver_ops_t aml_cpu_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_cpu::AmlCpu::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER(aml_cpu, aml_cpu_driver_ops, "zircon", "0.1");
