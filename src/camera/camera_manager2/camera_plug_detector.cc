// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera_plug_detector.h"

#include <fcntl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include <src/lib/fsl/io/device_watcher.h>
#include <src/lib/syslog/cpp/logger.h>

namespace camera {

constexpr char kDevicePath[] = "/dev/class/camera";

zx_status_t CameraPlugDetector::Start(CameraPlugDetector::OnDeviceEnumeratedCallback callback) {
  ZX_ASSERT(callback);
  on_device_enumerated_callback_ = std::move(callback);
  device_watcher_ = fsl::DeviceWatcher::Create(
      kDevicePath,
      [this](int dir_fd, const std::string& filename) { AddCameraDevice(dir_fd, filename); });
  if (device_watcher_ == nullptr) {
    FX_LOGS(ERROR) << " failed to create DeviceWatcher.";
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void CameraPlugDetector::Stop() { device_watcher_ = nullptr; }

void CameraPlugDetector::AddCameraDevice(int dir_fd, const std::string& filename) {
  if (!on_device_enumerated_callback_) {
    return;
  }
  // it seems like the watcher tells us about the '.' entry, so filter that out.
  if (filename == ".") {
    return;
  }

  // Open the device node.
  fbl::unique_fd dev_node(openat(dir_fd, filename.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FX_LOGS(ERROR) << "CameraPlugDetector failed to open device node at \"" << filename << "\". ("
                   << strerror(errno) << " : " << errno << ")";
    return;
  }

  zx_handle_t handle;
  FX_CHECK(ZX_OK == fdio_get_service_handle(dev_node.release(), &handle));
  fuchsia::hardware::camera::DevicePtr device;
  device.Bind(zx::channel(handle));
  device.set_error_handler([filename](zx_status_t res) {
    FX_PLOGS(ERROR, res) << "Failed to open channel to camera " << filename;
  });

  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;

  // Connect to the controller interface.
  device->GetChannel2(controller.NewRequest().TakeChannel());

  on_device_enumerated_callback_(std::move(controller));
}
}  // namespace camera
