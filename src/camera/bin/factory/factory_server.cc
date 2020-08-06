// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_server.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>

namespace camera {

namespace {
const char* kDevicePath = "/dev/class/isp/000";
}  // namespace

FactoryServer::FactoryServer() {}

FactoryServer::~FactoryServer() {
  if (isp_) {
    isp_.Unbind();
  }
}

fit::result<std::unique_ptr<FactoryServer>, zx_status_t> FactoryServer::Create() {
  auto server = std::make_unique<FactoryServer>();

  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error opening device at " << kDevicePath;
    return fit::error(ZX_ERR_IO);
  }
  fbl::unique_fd fd;
  fd.reset(result);

  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get handle for device at " << kDevicePath;
    return fit::error(ZX_ERR_UNAVAILABLE);
  }

  server->isp_.Bind(std::move(channel));

  return fit::ok(std::move(server));
}

void FactoryServer::GetOtpData() {
  zx_status_t status;
  zx::vmo vmo;
  isp_->GetOtpData(&status, &vmo);
  if (status != ZX_OK) {
    return;
  }
  size_t vmo_size;
  vmo.get_size(&vmo_size);
  std::vector<uint8_t> buf;
  vmo.read(buf.data(), 0, vmo_size);
  for (auto byte : buf) {
    FX_LOGS(INFO) << byte;
  }
}

void FactoryServer::GetSensorTemperature() {
  zx_status_t status;
  int32_t temp;
  isp_->GetSensorTemperature(&status, &temp);
  if (status != ZX_OK) {
    return;
  }
  FX_LOGS(INFO) << temp;
}

void FactoryServer::SetAWBMode(fuchsia::factory::camera::WhiteBalanceMode mode, uint32_t temp) {
  isp_->SetAWBMode(mode, temp);
}

void FactoryServer::SetAEMode(fuchsia::factory::camera::ExposureMode mode) {
  isp_->SetAEMode(mode);
}

void FactoryServer::SetExposure(float integration_time, float analog_gain, float digital_gain) {
  isp_->SetExposure(integration_time, analog_gain, digital_gain);
}

void FactoryServer::SetSensorMode(uint32_t mode) {
  isp_->SetSensorMode(mode);
}

void FactoryServer::SetTestPatternMode(uint16_t mode) {
  isp_->SetTestPatternMode(mode);
}

}  // namespace camera
