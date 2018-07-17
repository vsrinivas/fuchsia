// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"
#include <ddk/protocol/scpi.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <zircon/device/thermal.h>
#include "lib/component/cpp/startup_context.h"

namespace scpi {

static const char kThermalDir[] = "/dev/class/thermal";

App::App() : App(component::StartupContext::CreateFromStartupInfo()) {}

App::App(std::unique_ptr<component::StartupContext> context)
    : context_(std::move(context)) {}

App::~App() {}

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

  callback(fuchsia::scpi::Status::OK, std::move(info));
}

}  // namespace scpi
