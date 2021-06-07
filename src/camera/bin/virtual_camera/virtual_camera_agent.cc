// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/bin/virtual_camera/virtual_camera_agent.h"

#include <lib/syslog/cpp/macros.h>

using fuchsia::camera::test::virtualcamera::VirtualCameraDevice_AddToDeviceWatcher_Result;

namespace camera {

VirtualCameraAgent::VirtualCameraAgent(sys::ComponentContext* component_context)
    : component_context_(component_context), hardware_device_binding_(this) {
  device_watcher_tester_ =
      component_context_->svc()->Connect<fuchsia::camera::test::DeviceWatcherTester>();
  device_watcher_tester_.set_error_handler(
      [](zx_status_t status) { FX_LOGS(WARNING) << "Lost connection to DeviceWatcherTester"; });
}

void VirtualCameraAgent::GetChannel(zx::channel channel) {
  FX_LOGS(INFO) << "VirtualCameraAgent::GetChannel unimplemented";
}

void VirtualCameraAgent::GetChannel2(
    fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> server_end) {
  FX_LOGS(INFO) << "VirtualCameraAgent::GetChannel2 unimplemented";
}

void VirtualCameraAgent::AddToDeviceWatcher(AddToDeviceWatcherCallback callback) {
  if (hardware_device_binding_.is_bound()) {
    FX_LOGS(WARNING) << "AddToDeviceWatcher already called.";
    callback(VirtualCameraDevice_AddToDeviceWatcher_Result::WithErr(
        fuchsia::camera::test::virtualcamera::Error::ALREADY_ADDED_TO_DEVICE_WATCHER));
    return;
  }

  device_watcher_tester_->InjectDevice(hardware_device_binding_.NewBinding());
  callback(VirtualCameraDevice_AddToDeviceWatcher_Result::WithResponse({}));
}

}  // namespace camera
