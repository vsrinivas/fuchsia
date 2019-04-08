// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"

#include <ddk/protocol/scpi.h>
#include <fbl/unique_fd.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

namespace scpi {

static const char kThermalDir[] = "/dev/class/thermal";

App::App() : App(sys::ComponentContext::Create()) {}

App::App(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

App::~App() {}

zx::handle App::GetRootResource() {
  const int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd == 0)
    return {};

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    return {};
  }

  zx_handle_t root_resource;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, &root_resource);

  if (fidl_status != ZX_OK || status != ZX_OK)
    return {};

  return zx::handle(root_resource);
}

size_t App::ReadCpuCount(const zx::handle& root_resource) {
  size_t actual, available;
  zx_status_t err = root_resource.get_info(ZX_INFO_CPU_STATS, nullptr, 0,
                                           &actual, &available);
  if (err != ZX_OK) {
    return 0;
  }
  return available;
}

bool App::ReadCpuStats() {
  size_t actual, available;
  zx_status_t err = root_resource_handle_.get_info(
      ZX_INFO_CPU_STATS, &cpu_stats_[0], num_cores_ * sizeof(zx_info_cpu_stats),
      &actual, &available);
  return (err == ZX_OK);
}

bool App::ReadMemStats() {
  zx_status_t err =
      root_resource_handle_.get_info(ZX_INFO_KMEM_STATS, &mem_stats_,
                                     sizeof(zx_info_kmem_stats_t), NULL, NULL);
  return (err == ZX_OK);
}

zx_status_t App::Start() {
  fxl::UniqueFD dirfd(open(kThermalDir, O_DIRECTORY | O_RDONLY));
  if (!dirfd.is_valid()) {
    return ZX_ERR_NOT_DIR;
  }

  auto DeviceAdded = [](int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp("000", name)) {
      return ZX_ERR_STOP;
    } else {
      return ZX_OK;
    }
  };

  zx_status_t status =
      fdio_watch_directory(dirfd.get(), DeviceAdded, ZX_TIME_INFINITE, NULL);
  if (status != ZX_ERR_STOP) {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::unique_fd fd(open("/dev/class/thermal/000", O_RDWR));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open sensor " << errno;
    return ZX_ERR_UNAVAILABLE;
  }

  status = fdio_get_service_handle(fd.release(),
                                   thermal_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get handle for sensor " << errno;
    return ZX_ERR_UNAVAILABLE;
  }

  root_resource_handle_ = GetRootResource();
  num_cores_ = ReadCpuCount(root_resource_handle_);
  cpu_stats_.reserve(num_cores_);
  last_cpu_stats_.reserve(num_cores_);
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  return ZX_OK;
}

void App::GetDvfsInfo(const uint32_t power_domain,
                      GetDvfsInfoCallback callback) {
  fuchsia_hardware_thermal_OperatingPoint opps;
  auto result = fidl::VectorPtr<fuchsia::scpi::DvfsOpp>::New(0);
  zx_status_t status, status2;
  status = fuchsia_hardware_thermal_DeviceGetDvfsInfo(
      thermal_handle_.get(), power_domain, &status2, &opps);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", status,
            status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_INFO, std::move(result));
    return;
  }
  for (uint32_t i = 0; i < opps.count; i++) {
    fuchsia::scpi::DvfsOpp opp;
    opp.freq_hz = opps.opp[i].freq_hz;
    opp.volt_mv = opps.opp[i].volt_mv;
    result.push_back(std::move(opp));
  }
  callback(fuchsia::scpi::Status::OK, std::move(result));
}

void App::GetSystemStatus(GetSystemStatusCallback callback) {
  fuchsia::scpi::SystemStatus info;
  zx_status_t status, status2;
  uint16_t op_idx;
  status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      thermal_handle_.get(),
      fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status2,
      &op_idx);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %d %d\n", status,
            status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }
  info.big_cluster_op_index = op_idx;

  status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      thermal_handle_.get(),
      fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
      &status2, &op_idx);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %d %d\n", status,
            status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }
  info.small_cluster_op_index = op_idx;

  status = fuchsia_hardware_thermal_DeviceGetTemperature(
      thermal_handle_.get(), &status2, &info.temperature);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get current temperature: %d %d\n", status,
            status2);
    callback(fuchsia::scpi::Status::ERR_TEMPERATURE, std::move(info));
    return;
  }

  status = fuchsia_hardware_thermal_DeviceGetFanLevel(
      thermal_handle_.get(), &status2, &info.fan_level);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get fan level: %d %d\n", status, status2);
    callback(fuchsia::scpi::Status::ERR_FAN_LEVEL, std::move(info));
    return;
  }

  zx_time_t delay = ZX_SEC(1);

  if (!ReadCpuStats()) {
    fprintf(stderr, "ERROR: Failed to get cpu_stats_ \n");
    callback(fuchsia::scpi::Status::ERR_CPU_STATS, std::move(info));
    return;
  }

  last_cpu_stats_.swap(cpu_stats_);
  sleep(1);

  if (!ReadCpuStats()) {
    fprintf(stderr, "ERROR: Failed to get cpu_stats_ \n");
    callback(fuchsia::scpi::Status::ERR_CPU_STATS, std::move(info));
    return;
  }

  zx_time_t idle_time, busy_time;
  double busypercent_sum = 0;
  for (size_t i = 0; i < num_cores_; i++) {
    idle_time = cpu_stats_[i].idle_time - last_cpu_stats_[i].idle_time;
    busy_time = delay - (idle_time > delay ? delay : idle_time);
    double busypercent = (busy_time * 100) / (double)delay;
    busypercent_sum += busypercent;
  }

  info.cpu_utilization = busypercent_sum / num_cores_;

  if (!ReadMemStats()) {
    fprintf(stderr, "ERROR: Failed to get mem_stats_ \n");
    callback(fuchsia::scpi::Status::ERR_MEM_STATS, std::move(info));
    return;
  }

  info.memory_utilization = ((mem_stats_.total_bytes - mem_stats_.free_bytes) *
                             100 / mem_stats_.total_bytes);

  callback(fuchsia::scpi::Status::OK, std::move(info));
}

}  // namespace scpi
