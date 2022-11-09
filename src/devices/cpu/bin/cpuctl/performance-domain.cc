// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance-domain.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/component/cpp/incoming/service_client.h>

#include <iostream>

using fuchsia_device::wire::kMaxDevicePerformanceStates;

zx::result<CpuPerformanceDomain> CpuPerformanceDomain::CreateFromPath(const std::string& path) {
  zx::result cpu = component::Connect<cpuctrl::Device>(path);
  if (cpu.is_error()) {
    return cpu.take_error();
  }
  zx::result device = component::Connect<fuchsia_device::Controller>(path);
  if (device.is_error()) {
    return device.take_error();
  }
  return zx::ok(CpuPerformanceDomain(std::move(cpu.value()), std::move(device.value())));
}

std::pair<zx_status_t, uint64_t> CpuPerformanceDomain::GetNumLogicalCores() {
  auto resp = cpu_client_->GetNumLogicalCores();
  return std::make_pair(resp.status(), resp.status() == ZX_OK ? resp.value().count : 0);
}

std::tuple<zx_status_t, uint64_t, cpuctrl::wire::CpuPerformanceStateInfo>
CpuPerformanceDomain::GetCurrentPerformanceState() {
  constexpr cpuctrl::wire::CpuPerformanceStateInfo kEmptyPstate = {
      .frequency_hz = cpuctrl::wire::kFrequencyUnknown,
      .voltage_uv = cpuctrl::wire::kVoltageUnknown,
  };
  auto resp = device_client_->GetCurrentPerformanceState();

  if (resp.status() != ZX_OK) {
    return std::make_tuple(resp.status(), 0, kEmptyPstate);
  }

  const auto& pstates = GetPerformanceStates();

  uint64_t current_pstate = resp.value().out_state;

  cpuctrl::wire::CpuPerformanceStateInfo pstate_result = kEmptyPstate;
  if (current_pstate >= pstates.size()) {
    std::cerr << "No description for current pstate." << std::endl;
  } else {
    pstate_result = pstates[current_pstate];
  }

  return std::make_tuple(ZX_OK, current_pstate, pstate_result);
}

const std::vector<cpuctrl::wire::CpuPerformanceStateInfo>&
CpuPerformanceDomain::GetPerformanceStates() {
  // If we've already fetched this in the past, there's no need to fetch again.
  if (!cached_pstates_.empty()) {
    return cached_pstates_;
  }

  for (uint32_t i = 0; i < kMaxDevicePerformanceStates; i++) {
    auto resp = cpu_client_->GetPerformanceStateInfo(i);

    if (resp.status() != ZX_OK || resp->is_error()) {
      continue;
    }

    cached_pstates_.push_back(resp->value()->info);
  }

  return cached_pstates_;
}

zx_status_t CpuPerformanceDomain::SetPerformanceState(uint32_t new_performance_state) {
  if (new_performance_state >= kMaxDevicePerformanceStates) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = device_client_->SetPerformanceState(new_performance_state);

  if (result.status() != ZX_OK) {
    return result.status();
  }

  if (result.value().status != ZX_OK) {
    return result.value().status;
  }

  if (result.value().out_state != new_performance_state) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}
