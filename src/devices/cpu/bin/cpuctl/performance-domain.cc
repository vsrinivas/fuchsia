// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance-domain.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>

#include <iostream>

using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;

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

  cpuctrl::Device::SyncClient cpu_client(std::move(cpu_local));
  fuchsia_device::Controller::SyncClient device_client(std::move(device_local));

  CpuPerformanceDomain result(std::move(cpu_client), std::move(device_client));
  return result;
}

std::pair<zx_status_t, uint64_t> CpuPerformanceDomain::GetNumLogicalCores() {
  auto resp = cpu_client_.GetNumLogicalCores();
  return std::make_pair(resp.status(), resp.status() == ZX_OK ? resp.value().count : 0);
}

std::tuple<zx_status_t, uint64_t, cpuctrl::CpuPerformanceStateInfo>
CpuPerformanceDomain::GetCurrentPerformanceState() {
  constexpr cpuctrl::CpuPerformanceStateInfo kEmptyPstate = {
      .frequency_hz = cpuctrl::FREQUENCY_UNKNOWN,
      .voltage_uv = cpuctrl::VOLTAGE_UNKNOWN,
  };
  auto resp = device_client_.GetCurrentPerformanceState();

  if (resp.status() != ZX_OK) {
    return std::make_tuple(resp.status(), 0, kEmptyPstate);
  }

  const auto& pstates = GetPerformanceStates();

  uint64_t current_pstate = resp.value().out_state;

  cpuctrl::CpuPerformanceStateInfo pstate_result = kEmptyPstate;
  if (current_pstate >= pstates.size()) {
    std::cerr << "No description for current pstate." << std::endl;
  } else {
    pstate_result = pstates[current_pstate];
  }

  return std::make_tuple(ZX_OK, current_pstate, pstate_result);
}

const std::vector<cpuctrl::CpuPerformanceStateInfo>& CpuPerformanceDomain::GetPerformanceStates() {
  // If we've already fetched this in the past, there's no need to fetch again.
  if (!cached_pstates_.empty()) {
    return cached_pstates_;
  }

  for (uint32_t i = 0; i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
    auto resp = cpu_client_.GetPerformanceStateInfo(i);

    if (resp.status() != ZX_OK || resp->result.is_err()) {
      continue;
    }

    cached_pstates_.push_back(resp.value().result.response().info);
  }

  return cached_pstates_;
}

zx_status_t CpuPerformanceDomain::SetPerformanceState(uint32_t new_performance_state) {
  if (new_performance_state >= MAX_DEVICE_PERFORMANCE_STATES) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = device_client_.SetPerformanceState(new_performance_state);

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
