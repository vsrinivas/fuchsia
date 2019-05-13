// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher_impl.h"

#include <fcntl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/resource.h>
#include <trace/event.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"

namespace cobalt {

CpuStatsFetcherImpl::CpuStatsFetcherImpl() { InitializeRootResourceHandle(); }

bool CpuStatsFetcherImpl::FetchCpuPercentage(double *cpu_percentage) {
  TRACE_DURATION("system_metrics", "CpuStatsFetcherImpl::FetchCpuPercentage");
  if (FetchCpuStats() == false) {
    return false;
  }
  bool success = CalculateCpuPercentage(cpu_percentage);
  last_cpu_stats_.swap(cpu_stats_);

  last_cpu_fetch_time_ = cpu_fetch_time_;
  return success;
}

bool CpuStatsFetcherImpl::FetchCpuStats() {
  if (root_resource_handle_ == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "CpuStatsFetcherImpl: No root resource "
                   << "present. Reconnecting...";
    InitializeRootResourceHandle();
    return false;
  }
  size_t actual, available;
  cpu_fetch_time_ = std::chrono::high_resolution_clock::now();
  zx_status_t err = zx_object_get_info(
      root_resource_handle_, ZX_INFO_CPU_STATS, &cpu_stats_[0],
      cpu_stats_.size() * sizeof(zx_info_cpu_stats_t), &actual, &available);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "CpuStatsFetcherImpl: Fetching "
                   << "ZX_INFO_CPU_STATS through syscall returns "
                   << zx_status_get_string(err);
    return false;
  }
  if (actual < available) {
    FXL_LOG(WARNING) << "CpuStatsFetcherImpl:  actual CPUs reported " << actual
                     << " is less than available CPUs " << available
                     << ". Please increase zx_info_cpu_stats_t vector size!"
                     << sizeof(cpu_stats_);
    return false;
  }
  if (num_cpu_cores_ == 0) {
    num_cpu_cores_ = actual;
  }
  return true;
}

bool CpuStatsFetcherImpl::CalculateCpuPercentage(double *cpu_percentage) {
  if (last_cpu_stats_.empty()) {
    return false;
  }
  auto elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          cpu_fetch_time_ - last_cpu_fetch_time_)
                          .count();
  double cpu_percentage_sum = 0;
  for (size_t i = 0; i < num_cpu_cores_; i++) {
    zx_duration_t delta_idle_time = zx_duration_sub_duration(
        cpu_stats_[i].idle_time, last_cpu_stats_[i].idle_time);
    zx_duration_t delta_busy_time =
        (delta_idle_time > elapsed_time ? 0 : elapsed_time - delta_idle_time);
    cpu_percentage_sum += static_cast<double>(delta_busy_time) * 100 /
                          static_cast<double>(elapsed_time);
  }
  *cpu_percentage = cpu_percentage_sum / static_cast<double>(num_cpu_cores_);
  return true;
}

// TODO(CF-691) When Component Stats (CS) supports cpu metrics,
// switch to Component Stats / iquery, by creating a new class with the
// interface CpuStatsFetcher.
void CpuStatsFetcherImpl::InitializeRootResourceHandle() {
  static const char kSysInfo[] = "/dev/misc/sysinfo";
  int fd = open(kSysInfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << "Cannot open sysinfo: " << strerror(errno);
    return;
  }
  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << "Cannot obtain sysinfo channel: " << zx_status_get_string(status);
    return;
  }
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, &root_resource_handle_);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << zx_status_get_string(fidl_status);
    return;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << zx_status_get_string(status);
    return;
  } else if (root_resource_handle_ == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Failed to get root_resource_handle_.";
    return;
  }
}

}  // namespace cobalt
