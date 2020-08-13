// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CPU_BIN_CPUCTL_PERFORMANCE_DOMAIN_H_
#define SRC_DEVICES_CPU_BIN_CPUCTL_PERFORMANCE_DOMAIN_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>

#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace cpuctrl = ::llcpp::fuchsia::hardware::cpu::ctrl;
namespace fuchsia_device = ::llcpp::fuchsia::device;

class CpuPerformanceDomain {
 public:
  static std::variant<zx_status_t, CpuPerformanceDomain> CreateFromPath(const std::string& path);
  std::pair<zx_status_t, uint64_t> GetNumLogicalCores();
  std::tuple<zx_status_t, uint64_t, cpuctrl::CpuPerformanceStateInfo> GetCurrentPerformanceState();
  const std::vector<cpuctrl::CpuPerformanceStateInfo>& GetPerformanceStates();
  zx_status_t SetPerformanceState(uint32_t new_performance_state);

 protected:
  // Don't allow explicit construction.
  explicit CpuPerformanceDomain(cpuctrl::Device::SyncClient cpu_client,
                                fuchsia_device::Controller::SyncClient device_client)
      : cpu_client_(std::move(cpu_client)), device_client_(std::move(device_client)) {}

  cpuctrl::Device::SyncClient cpu_client_;
  fuchsia_device::Controller::SyncClient device_client_;

  // Don't use this directly. Instead call GetPerformanceStates().
  std::vector<cpuctrl::CpuPerformanceStateInfo> cached_pstates_;
};

#endif  // SRC_DEVICES_CPU_BIN_CPUCTL_PERFORMANCE_DOMAIN_H_
