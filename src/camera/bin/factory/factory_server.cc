// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/factory_server.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>

namespace camera {

namespace {
const char* kDevicePath = "/dev/class/isp/000";
}  // namespace

FactoryServer::FactoryServer() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

FactoryServer::~FactoryServer() {
  loop_.RunUntilIdle();
  if (isp_) {
    isp_.Unbind();
  }
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<FactoryServer>, zx_status_t> FactoryServer::Create(
    std::unique_ptr<Streamer> streamer, fit::closure stop_callback) {
  auto server = std::make_unique<FactoryServer>();

  server->stop_callback_ = std::move(stop_callback);

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

  server->isp_.Bind(std::move(channel), server->loop_.dispatcher());

  // Start a thread and begin processing messages.
  status = server->loop_.StartThread("camera-factory Loop");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  server->streamer_ = std::move(streamer);

  return fit::ok(std::move(server));
}

void FactoryServer::GetOtpData() {
  isp_->GetOtpData([](zx_status_t get_otp_status, size_t byte_count, zx::vmo otp_data) {
    if (get_otp_status != ZX_OK) {
      return;
    }
    uint8_t buf[byte_count];
    otp_data.read(buf, 0, byte_count);
    for (auto byte : buf) {
      std::cout << byte;
    }
  });
}

void FactoryServer::GetSensorTemperature() {
  isp_->GetSensorTemperature([](zx_status_t get_sensor_temperature_status, int32_t temperature) {
    if (get_sensor_temperature_status != ZX_OK) {
      return;
    }
    FX_LOGS(INFO) << temperature;
  });
}

void FactoryServer::SetAWBMode(fuchsia::factory::camera::WhiteBalanceMode mode, uint32_t temp) {
  isp_->SetAWBMode(mode, temp, []() { return; });
}

void FactoryServer::SetAEMode(fuchsia::factory::camera::ExposureMode mode) {
  isp_->SetAEMode(mode, []() { return; });
}

void FactoryServer::SetExposure(float integration_time, float analog_gain, float digital_gain) {
  isp_->SetExposure(integration_time, analog_gain, digital_gain, []() { return; });
}

void FactoryServer::SetSensorMode(uint32_t mode) {
  isp_->SetSensorMode(mode, []() { return; });
}

void FactoryServer::SetTestPatternMode(uint16_t mode) {
  isp_->SetTestPatternMode(mode, []() { return; });
}

}  // namespace camera
