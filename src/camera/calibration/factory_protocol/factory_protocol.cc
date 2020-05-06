// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <fcntl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>
#include <src/lib/syslog/cpp/logger.h>

namespace camera {

inline constexpr auto kTag = "camera_factory_server";
inline constexpr auto kDirPath = "/calibration";

inline constexpr auto kCameraPath = "/dev/class/camera";

static fit::result<fuchsia::hardware::camera::DeviceHandle, zx_status_t> GetCamera(
    const std::string& path) {
  fuchsia::hardware::camera::DeviceHandle camera;
  zx_status_t status =
      fdio_service_connect(path.c_str(), camera.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  return fit::ok(std::move(camera));
}

fit::result<std::unique_ptr<FactoryServer>, zx_status_t> FactoryServer::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  auto factory_server = std::make_unique<FactoryServer>();

  auto result = GetCamera(kCameraPath);
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Couldn't get camera from " << kCameraPath;
    return fit::error(result.error());
  }
  auto camera = result.take_value();
  fuchsia::hardware::camera::DeviceSyncPtr dev;
  dev.Bind(std::move(camera));
  zx_status_t status = dev->GetChannel2(controller.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    return fit::error(status);
  }

  status =
      factory_server->controller_.Bind(std::move(controller), factory_server->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get controller interface from device";
    return fit::error(status);
  }
  factory_server->controller_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Controller server disconnected during initialization.";
  });

  return fit::ok(std::move(factory_server));
}

void FactoryServer::ConnectToStream() { FX_NOTIMPLEMENTED(); };

// |fuchsia::camera2::Stream|

void FactoryServer::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo /* info */) {
  FX_NOTIMPLEMENTED();
}

// |fuchsia::factory::camera::CameraFactory|

void FactoryServer::DetectCamera(DetectCameraCallback /* callback */) { FX_NOTIMPLEMENTED(); }

void FactoryServer::Start() { FX_NOTIMPLEMENTED(); }

void FactoryServer::Stop() { FX_NOTIMPLEMENTED(); }

void FactoryServer::SetConfig(uint32_t /* mode */, int32_t /* integration_time */,
                              int32_t /* analog_gain */, int32_t /* digital_gain */,
                              SetConfigCallback /* callback */) {
  FX_NOTIMPLEMENTED();
}

void FactoryServer::CaptureImage(CaptureImageCallback /* callback */) { FX_NOTIMPLEMENTED(); }

void FactoryServer::WriteCalibrationData(fuchsia::mem::Buffer /* calibration_data */,
                                         std::string /* file_path */,
                                         WriteCalibrationDataCallback /* callback */) {
  FX_NOTIMPLEMENTED();
}

}  // namespace camera
