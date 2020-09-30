// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <lib/device-protocol/pdev.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/mmio/mmio.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>

namespace amlogic_cpu {

namespace {
constexpr size_t kFragmentPdev = 0;
// Fragments are provided to this driver in groups of 4. Fragments are provided as follows:
// 0 - Platform Device
// [4 fragments for cluster 0]
// [4 fragments for cluster 1]
// [...]
// [4 fragments for cluster n]
// The following offsets refer to the offset of the fragment inside its group of 4.
// i.e. power will always be first, followed by plldiv16clk, cpudiv16clk, and finally cpuscaler
constexpr size_t kPowerFragmentOffset = 1;
constexpr size_t kPllDiv16ClkFragmentOffset = 2;
constexpr size_t kCpuDiv16ClkFragmentOffset = 3;
constexpr size_t kCpuScalerClkFragmentOffset = 4;
constexpr size_t kFragmentsPerPfDomain = 4;

constexpr zx_off_t kCpuVersionOffset = 0x220;
}  // namespace

zx_status_t AmlCpu::Create(void* context, zx_device_t* parent) {
  zx_status_t st;
  size_t actual;

  // Get the metadata for the performance domains.
  size_t perf_domain_size = 0;
  st = device_get_metadata_size(parent, DEVICE_METADATA_AML_PERF_DOMAINS, &perf_domain_size);
  zxlogf(DEBUG, "%s: Got AML_PERF_DOMAINS metadata size, st = %d, size = %lu", __func__, st,
         perf_domain_size);

  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get performance domain count from board driver, st = %d", __func__,
           st);
    return st;
  }

  // Make sure that the board driver gave us an exact integer number of performance domains.
  if (perf_domain_size % sizeof(perf_domain_t) != 0) {
    zxlogf(ERROR,
           "%s: Performance domain metadata from board driver is malformed. perf_domain_size = "
           "%lu, sizeof(perf_domain_t) = %lu",
           __func__, perf_domain_size, sizeof(perf_domain_t));
    return ZX_ERR_INTERNAL;
  }

  const size_t num_perf_domains = perf_domain_size / sizeof(perf_domain_t);
  std::unique_ptr<perf_domain_t[]> perf_domains =
      std::make_unique<perf_domain_t[]>(num_perf_domains);
  st = device_get_metadata(parent, DEVICE_METADATA_AML_PERF_DOMAINS, perf_domains.get(),
                           perf_domain_size, &actual);
  zxlogf(DEBUG, "%s: Got AML_PERF_DOMAINS metadata, st = %d, actual = %lu", __func__, st, actual);

  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get performance domain metadata from board driver, st = %d",
           __func__, st);
    return st;
  }
  if (actual != perf_domain_size) {
    zxlogf(ERROR, "%s: Expected %lu bytes in perf domain metadata, got %lu", __func__,
           perf_domain_size, actual);
    return ZX_ERR_INTERNAL;
  }

  size_t op_point_size;
  st = device_get_metadata_size(parent, DEVICE_METADATA_AML_OP_POINTS, &op_point_size);
  zxlogf(DEBUG, "%s: Got AML_OP_POINTS metadata size, st = %d, size = %lu", __func__, st,
         op_point_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get opp count metadata size from board driver, st = %d", __func__,
           st);
    return st;
  }

  if (op_point_size % sizeof(operating_point_t) != 0) {
    zxlogf(ERROR, "%s: Operating point metadata from board driver is malformed", __func__);
    return ZX_ERR_INTERNAL;
  }

  const size_t num_op_points = op_point_size / sizeof(operating_point_t);
  std::unique_ptr<operating_point_t[]> operating_points =
      std::make_unique<operating_point_t[]>(num_op_points);
  st = device_get_metadata(parent, DEVICE_METADATA_AML_OP_POINTS, operating_points.get(),
                           op_point_size, &actual);
  zxlogf(DEBUG, "%s: Got AML_OP_POINTS metadata, st = %d, actual = %lu", __func__, st, actual);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get operating points from board driver, st = %d", __func__, st);
    return st;
  }
  if (actual != op_point_size) {
    zxlogf(ERROR, "%s: Expected %lu bytes in operating point metadata, got %lu", __func__,
           op_point_size, actual);
    return ZX_ERR_INTERNAL;
  }

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: failed to get composite protocol", __func__);
    return ZX_ERR_INTERNAL;
  }

  // Make sure we have the right number of fragments.
  const size_t fragment_count = composite.GetFragmentCount();
  zxlogf(DEBUG, "%s: GetFragmentCount = %lu", __func__, fragment_count);
  if ((num_perf_domains * kFragmentsPerPfDomain) + 1 != fragment_count) {
    zxlogf(ERROR,
           "%s: Expected %lu fragments for each %lu performance domains for a total of %lu "
           "fragments but got %lu instead",
           __func__, kFragmentsPerPfDomain, perf_domain_size,
           perf_domain_size * kFragmentsPerPfDomain, fragment_count);
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<zx_device_t*[]> fragments = std::make_unique<zx_device_t*[]>(fragment_count);
  composite.GetFragments(fragments.get(), fragment_count, &actual);
  zxlogf(DEBUG, "%s: GetFragments = %lu", __func__, actual);
  if (actual != fragment_count) {
    zxlogf(ERROR, "%s: Expected to get %lu fragments, actually got %lu", __func__, fragment_count,
           actual);
    return ZX_ERR_INTERNAL;
  }

  // Map AOBUS registers
  ddk::PDev pdev(fragments[kFragmentPdev]);
  std::optional<ddk::MmioBuffer> mmio_buffer;
  if ((st = pdev.MapMmio(0, &mmio_buffer)) != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to map mmio, st = %d", st);
    return st;
  }
  const uint32_t cpu_version_packed = mmio_buffer->Read32(kCpuVersionOffset);

  // Build and publish each performance domain.
  for (size_t i = 0; i < num_perf_domains; i++) {
    const perf_domain_t& perf_domain = perf_domains[i];

    const size_t power_fragment_offset = (kFragmentsPerPfDomain * i) + kPowerFragmentOffset;
    const size_t pll_div16_clk_fragment_offset =
        (kFragmentsPerPfDomain * i) + kPllDiv16ClkFragmentOffset;
    const size_t cpu_div16_clk_fragment_offset =
        (kFragmentsPerPfDomain * i) + kCpuDiv16ClkFragmentOffset;
    const size_t cpu_scaler_clk_fragment_offset =
        (kFragmentsPerPfDomain * i) + kCpuScalerClkFragmentOffset;

    ddk::ClockProtocolClient pllDiv16Client;
    if ((st = ddk::ClockProtocolClient::CreateFromDevice(fragments[pll_div16_clk_fragment_offset],
                                                         &pllDiv16Client)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to create pll_div_16 clock client, st = %d", __func__, st);
      return st;
    }

    ddk::ClockProtocolClient cpuDiv16Client;
    if ((st = ddk::ClockProtocolClient::CreateFromDevice(fragments[cpu_div16_clk_fragment_offset],
                                                         &cpuDiv16Client)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to create cpu_div_16 clock client, st = %d", __func__, st);
      return st;
    }

    ddk::ClockProtocolClient cpuScalerClient;
    if ((st = ddk::ClockProtocolClient::CreateFromDevice(fragments[cpu_scaler_clk_fragment_offset],
                                                         &cpuScalerClient)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to create cpu_scaler clock client, st = %d", __func__, st);
      return st;
    }

    ddk::PowerProtocolClient powerClient;
    if ((st = ddk::PowerProtocolClient::CreateFromDevice(fragments[power_fragment_offset],
                                                         &powerClient)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to create power client, st = %d", __func__, st);
      return st;
    }

    // Vector of operating points that belong to this power domain.
    std::vector<operating_point_t> pd_op_points;
    std::copy_if(operating_points.get(), operating_points.get() + num_op_points,
                 std::back_inserter(pd_op_points), [&perf_domain](const operating_point_t& op) {
                   return op.pd_id == perf_domain.id;
                 });

    // Order operating points from highest frequency to lowest because Operating Point 0 is the
    // fastest.
    std::sort(pd_op_points.begin(), pd_op_points.end(),
              [](const operating_point_t& a, const operating_point_t& b) {
                return a.freq_hz > b.freq_hz;
              });

    const size_t perf_state_count = pd_op_points.size();
    auto perf_states = std::make_unique<device_performance_state_info_t[]>(perf_state_count);
    for (size_t j = 0; j < perf_state_count; j++) {
      perf_states[j].state_id = static_cast<uint8_t>(j);
      perf_states[j].restore_latency = 0;
    }

    auto device = std::make_unique<AmlCpu>(parent, std::move(pllDiv16Client),
                                           std::move(cpuDiv16Client), std::move(cpuScalerClient),
                                           std::move(powerClient), std::move(pd_op_points));

    st = device->Init();
    if (st != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to initialize device, st = %d", __func__, st);
      return st;
    }

    device->SetCpuInfo(cpu_version_packed);

    st = device->DdkAdd(ddk::DeviceAddArgs("cpu")
                            .set_flags(DEVICE_ADD_NON_BINDABLE)
                            .set_proto_id(ZX_PROTOCOL_CPU_CTRL)
                            .set_performance_states({perf_states.get(), perf_state_count})
                            .set_inspect_vmo(device->inspector_.DuplicateVmo()));

    if (st != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed, st = %d", __func__, st);
      return st;
    }

    __UNUSED auto ptr = device.release();
  }

  return ZX_OK;
}

zx_status_t AmlCpu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_cpuctrl::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlCpu::DdkRelease() { delete this; }

zx_status_t AmlCpu::DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state) {
  if (requested_state >= operating_points_.size()) {
    zxlogf(ERROR, "%s: Requested performance state is out of bounds, state = %u\n", __func__,
           requested_state);
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!out_state) {
    zxlogf(ERROR, "%s: out_state may not be null", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // There is no condition under which this function will return ZX_OK but out_state will not
  // be requested_state so we're going to go ahead and set that up front.
  *out_state = requested_state;

  const operating_point_t& target_state = operating_points_[requested_state];
  const operating_point_t& initial_state = operating_points_[current_pstate_];

  zxlogf(INFO, "%s: Scaling from %u MHz %u mV to %u MHz %u mV", __func__,
         initial_state.freq_hz / 1000000, initial_state.volt_uv / 1000,
         target_state.freq_hz / 1000000, target_state.volt_uv / 1000);

  if (initial_state.freq_hz == target_state.freq_hz &&
      initial_state.volt_uv == target_state.volt_uv) {
    // Nothing to be done.
    return ZX_OK;
  }

  zx_status_t st;
  if (target_state.freq_hz > initial_state.freq_hz) {
    // If we're increasing the frequency, we need to increase the voltage first.
    uint32_t actual_voltage;
    st = pwr_.RequestVoltage(target_state.volt_uv, &actual_voltage);
    if (st != ZX_OK || actual_voltage != target_state.volt_uv) {
      zxlogf(ERROR, "%s: Failed to set cpu voltage, requested = %u, got = %u, st = %d", __func__,
             target_state.volt_uv, actual_voltage, st);
      return st;
    }
  }

  // Set the frequency next.
  st = cpuscaler_.SetRate(target_state.freq_hz);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Could not set CPU frequency, st = %d\n", __func__, st);

    // Put the voltage back if frequency scaling fails.
    uint32_t actual_voltage;
    zx_status_t vt_st = pwr_.RequestVoltage(initial_state.volt_uv, &actual_voltage);
    if (vt_st != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to reset CPU voltage, st = %d, Voltage and frequency mismatch!",
             __func__, vt_st);
      return vt_st;
    }
    return st;
  }

  // If we're decreasing the frequency, then we set the voltage after the frequency has
  // been reduced.
  if (target_state.freq_hz < initial_state.freq_hz) {
    // If we're increaing the frequency, we need to increase the voltage first.
    uint32_t actual_voltage;
    st = pwr_.RequestVoltage(target_state.volt_uv, &actual_voltage);
    if (st != ZX_OK || actual_voltage != target_state.volt_uv) {
      zxlogf(ERROR,
             "%s: Failed to set cpu voltage, requested = %u, got = %u, st = %d. "
             "Voltage and frequency mismatch!",
             __func__, target_state.volt_uv, actual_voltage, st);
      return st;
    }
  }

  zxlogf(INFO, "%s: Success\n", __func__);

  current_pstate_ = requested_state;

  return ZX_OK;
}

zx_status_t AmlCpu::Init() {
  zx_status_t result;
  constexpr uint32_t kInitialPstate = 0;

  result = plldiv16_.Enable();
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable plldiv16, st = %d", __func__, result);
    return result;
  }

  result = cpudiv16_.Enable();
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable cpudiv16_, st = %d", __func__, result);
    return result;
  }

  uint32_t min_voltage, max_voltage;
  pwr_.GetSupportedVoltageRange(&min_voltage, &max_voltage);
  pwr_.RegisterPowerDomain(min_voltage, max_voltage);

  uint32_t actual;
  result = DdkSetPerformanceState(kInitialPstate, &actual);

  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set initial performance state, st = %d", __func__, result);
    return result;
  }

  if (actual != kInitialPstate) {
    zxlogf(ERROR, "%s: Failed to set initial performance state, requested = %u, actual = %u",
           __func__, kInitialPstate, actual);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t AmlCpu::DdkConfigureAutoSuspend(bool enable, uint8_t requested_sleep_state) {
  return ZX_ERR_NOT_SUPPORTED;
}

void AmlCpu::GetPerformanceStateInfo(uint32_t state,
                                     GetPerformanceStateInfoCompleter::Sync& completer) {
  if (state >= operating_points_.size()) {
    zxlogf(ERROR, "%s: Requested an operating point that's out of bounds, %u\n", __func__, state);
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  llcpp::fuchsia::hardware::cpu::ctrl::CpuPerformanceStateInfo result;
  result.frequency_hz = operating_points_[state].freq_hz;
  result.voltage_uv = operating_points_[state].volt_uv;

  completer.ReplySuccess(result);
}

void AmlCpu::GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync& completer) {
  unsigned int result = zx_system_get_num_cpus();
  completer.Reply(result);
}

void AmlCpu::GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync& completer) {
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
ZIRCON_DRIVER_BEGIN(aml_cpu, aml_cpu_driver_ops, "zircon", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_GOOGLE_AMLOGIC_CPU),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_LUIS),
ZIRCON_DRIVER_END(aml_cpu)
