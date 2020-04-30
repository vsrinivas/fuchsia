// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <ddk/protocol/scpi.h>
#include <fbl/unique_fd.h>

namespace scpi {

static const char kThermalDir[] = "/dev/class/thermal";

App::App() : App(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

App::App(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {}

App::~App() {}

std::unique_ptr<llcpp::fuchsia::kernel::Stats::SyncClient> App::GetStatsService() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return nullptr;
  }
  status = fdio_service_connect("/svc/fuchsia.kernel.Stats", remote.release());
  if (status != ZX_OK) {
    return nullptr;
  }

  cpu_stats_buffer_ =
      std::make_unique<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetCpuStatsResponse>>();
  last_cpu_stats_buffer_ =
      std::make_unique<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetCpuStatsResponse>>();
  mem_stats_buffer_ =
      std::make_unique<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetMemoryStatsResponse>>();
  return std::make_unique<llcpp::fuchsia::kernel::Stats::SyncClient>(std::move(local));
}

bool App::ReadCpuStats() {
  auto result = stats_->GetCpuStats(cpu_stats_buffer_->view());
  if (result.status() == ZX_OK) {
    cpu_stats_ = &result->stats;
  }
  return result.status() != ZX_OK;
}

bool App::ReadMemStats() {
  auto result = stats_->GetMemoryStats(mem_stats_buffer_->view());
  if (result.status() == ZX_OK) {
    mem_stats_ = &result->stats;
  }
  return result.status() != ZX_OK;
}

zx_status_t App::Start() {
  fbl::unique_fd dirfd(open(kThermalDir, O_DIRECTORY | O_RDONLY));
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

  zx_status_t status = fdio_watch_directory(dirfd.get(), DeviceAdded, ZX_TIME_INFINITE, NULL);
  if (status != ZX_ERR_STOP) {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::unique_fd fd(open("/dev/class/thermal/000", O_RDWR));
  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open sensor " << errno;
    return ZX_ERR_UNAVAILABLE;
  }

  status = fdio_get_service_handle(fd.release(), thermal_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get handle for sensor " << errno;
    return ZX_ERR_UNAVAILABLE;
  }

  stats_ = GetStatsService();
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  return ZX_OK;
}

void App::GetDvfsInfo(const uint32_t power_domain, GetDvfsInfoCallback callback) {
  fuchsia_hardware_thermal_OperatingPoint opps;
  std::vector<fuchsia::scpi::DvfsOpp> result;
  zx_status_t status, status2;
  status = fuchsia_hardware_thermal_DeviceGetDvfsInfo(thermal_handle_.get(), power_domain, &status2,
                                                      &opps);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %d %d\n", status, status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_INFO, std::move(result));
    return;
  }
  for (uint32_t i = 0; i < opps.count; i++) {
    fuchsia::scpi::DvfsOpp opp;
    opp.freq_hz = opps.opp[i].freq_hz;
    opp.volt_uv = opps.opp[i].volt_uv;
    result.push_back(std::move(opp));
  }
  callback(fuchsia::scpi::Status::OK, std::move(result));
}

void App::GetSystemStatus(GetSystemStatusCallback callback) {
  fuchsia::scpi::SystemStatus info;
  zx_status_t status, status2;
  uint16_t op_idx;
  status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      thermal_handle_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN,
      &status2, &op_idx);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %d %d\n", status, status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }
  info.big_cluster_op_index = op_idx;

  status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      thermal_handle_.get(), fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
      &status2, &op_idx);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %d %d\n", status, status2);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }
  info.small_cluster_op_index = op_idx;

  status = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(thermal_handle_.get(), &status2,
                                                                &info.temperature_celsius);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "ERROR: Failed to get current temperature: %d %d\n", status, status2);
    callback(fuchsia::scpi::Status::ERR_TEMPERATURE, std::move(info));
    return;
  }

  status =
      fuchsia_hardware_thermal_DeviceGetFanLevel(thermal_handle_.get(), &status2, &info.fan_level);
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

  last_cpu_stats_buffer_.swap(cpu_stats_buffer_);
  last_cpu_stats_ = cpu_stats_;
  sleep(1);

  if (!ReadCpuStats()) {
    fprintf(stderr, "ERROR: Failed to get cpu_stats_ \n");
    callback(fuchsia::scpi::Status::ERR_CPU_STATS, std::move(info));
    return;
  }

  zx_time_t idle_time, busy_time;
  double busypercent_sum = 0;
  size_t num_cores =
      std::min(cpu_stats_->per_cpu_stats.count(), last_cpu_stats_->per_cpu_stats.count());
  for (size_t i = 0; i < num_cores; i++) {
    idle_time =
        cpu_stats_->per_cpu_stats[i].idle_time() - last_cpu_stats_->per_cpu_stats[i].idle_time();
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

  info.memory_utilization =
      ((mem_stats_->total_bytes() - mem_stats_->free_bytes()) * 100 / mem_stats_->total_bytes());

  callback(fuchsia::scpi::Status::OK, std::move(info));
}

}  // namespace scpi
