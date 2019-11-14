// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_CAMERA_PLUG_DETECTOR_H_
#define SRC_CAMERA_CAMERA_MANAGER2_CAMERA_PLUG_DETECTOR_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <src/lib/fsl/io/device_watcher.h>

namespace camera {

// Detects devices that show up at /dev/class/camera, and gets the controller
// interface handle for them.
class CameraPlugDetector {
 public:
  using OnDeviceEnumeratedCallback =
      fit::function<void(fidl::InterfaceHandle<fuchsia::camera2::hal::Controller>)>;

  // Start listening for devices. |callback| will be called for each device that is
  // detected, including pre-existing devices.
  zx_status_t Start(OnDeviceEnumeratedCallback callback);

  // Stop listening for devices
  void Stop();

 private:
  void AddCameraDevice(int dir_fd, const std::string& filename);

  OnDeviceEnumeratedCallback on_device_enumerated_callback_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
};
}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER2_CAMERA_PLUG_DETECTOR_H_
