// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance-domain.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>

#include <iostream>

using fuchsia_device::wire::kMaxDevicePerformanceStates;

std::variant<zx_status_t, CpuPerformanceDomain> CpuPerformanceDomain::CreateFromPath(
    const std::string& path) {
  // Obtain a channel to the service.
  zx::channel cpu_local, cpu_remote, device_local, device_remote;
  zx_status_t st = zx::channel::create(0, &cpu_local, &cpu_remote);
  if (st != ZX_OK) {
    return st;
  }

  st = fdio_service_connect(path.c_str(), cpu_remote.release());
  if (st != ZX_OK) {
    return st;
  }

  st = zx::channel::create(0, &device_local, &device_remote);
  if (st != ZX_OK) {
    return st;
  }

  st = fdio_service_connect(path.c_str(), device_remote.release());
  if (st != ZX_OK) {
    return st;
  }

  fidl::WireSyncClient<cpuctrl::Device> cpu_client(std::move(cpu_local));
  fidl::WireSyncClient<fuchsia_device::Controller> device_client(std::move(device_local));

  CpuPerformanceDomain result(std::move(cpu_client), std::move(device_client));
  return result;
}

std::pair<zx_status_t, uint64_t> CpuPerformanceDomain::GetNumLogicalCores() {
  auto resp = cpu_client_->GetNumLogicalCores();
  return std::make_pair(resp.status(), resp.status() == ZX_OK ? resp.value_NEW().count : 0);
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

  uint64_t current_pstate = resp.value_NEW().out_state;

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

    if (resp.status() != ZX_OK || resp.Unwrap_NEW()->is_error()) {
      continue;
    }

    cached_pstates_.push_back(resp.Unwrap_NEW()->value()->info);
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

  if (result.value_NEW().status != ZX_OK) {
    return result.value_NEW().status;
  }

  if (result.value_NEW().out_state != new_performance_state) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}
