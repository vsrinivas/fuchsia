// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"
#include <ddk/protocol/scpi.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <zircon/device/sysinfo.h>
#include <zircon/device/thermal.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>
#include "lib/component/cpp/startup_context.h"

namespace scpi {

static const char kThermalDir[] = "/dev/class/thermal";

App::App() : App(component::StartupContext::CreateFromStartupInfo()) {}

App::App(std::unique_ptr<component::StartupContext> context)
    : context_(std::move(context)) {}

App::~App() {}

zx::handle App::GetRootResource() {
  const int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd == 0)
    return {};

  zx_handle_t root_resource;
  auto n = ioctl_sysinfo_get_root_resource(fd, &root_resource);
  close(fd);

  FXL_DCHECK(n == sizeof(root_resource));

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
      ZX_INFO_CPU_STATS, &cpu_stats_[0],
      num_cores_ * sizeof(zx_info_cpu_stats), &actual, &available);

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

  fd_.reset(open("/dev/class/thermal/000", O_RDWR));
  if (!fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open sensor " << errno;
    return ZX_ERR_UNAVAILABLE;
  }

  root_resource_handle_ = GetRootResource();
  num_cores_ = ReadCpuCount(root_resource_handle_);
  cpu_stats_.reserve(num_cores_);
  last_cpu_stats_.reserve(num_cores_);
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  return ZX_OK;
}

void App::GetDvfsInfo(const uint32_t power_domain,
                      GetDvfsInfoCallback callback) {
  scpi_opp_t opps;
  auto result = fidl::VectorPtr<fuchsia::scpi::DvfsOpp>::New(0);
  size_t rc = ioctl_thermal_get_dvfs_info(fd_.get(), &power_domain, &opps);
  if (rc != sizeof(opps)) {
    fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
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
  uint32_t power_domain = BIG_CLUSTER_POWER_DOMAIN;
  size_t rc = ioctl_thermal_get_dvfs_opp(fd_.get(), &power_domain,
                                         &info.big_cluster_op_index);
  if (rc != sizeof(uint32_t)) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %zd\n", rc);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }

  power_domain = LITTLE_CLUSTER_POWER_DOMAIN;
  rc = ioctl_thermal_get_dvfs_opp(fd_.get(), &power_domain,
                                  &info.small_cluster_op_index);
  if (rc != sizeof(uint32_t)) {
    fprintf(stderr, "ERROR: Failed to get dvfs opp idx: %zd\n", rc);
    callback(fuchsia::scpi::Status::ERR_DVFS_OPP_IDX, std::move(info));
    return;
  }

  rc = ioctl_thermal_get_temperature(fd_.get(), &info.temperature);
  if (rc != sizeof(uint32_t)) {
    fprintf(stderr, "ERROR: Failed to get current temperature: %zd\n", rc);
    callback(fuchsia::scpi::Status::ERR_TEMPERATURE, std::move(info));
    return;
  }

  rc = ioctl_thermal_get_fan_level(fd_.get(), &info.fan_level);
  if (rc != sizeof(uint32_t)) {
    fprintf(stderr, "ERROR: Failed to get fan level: %zd\n", rc);
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
  for (size_t i = 0; i<num_cores_; i++) {
    idle_time = cpu_stats_[i].idle_time - last_cpu_stats_[i].idle_time;
    busy_time = delay - (idle_time > delay? delay : idle_time);
    double busypercent = (busy_time * 100)/(double)delay;
    busypercent_sum += busypercent;
  }

  info.cpu_utilization = busypercent_sum/num_cores_;

  callback(fuchsia::scpi::Status::OK, std::move(info));
}

}  // namespace scpi
