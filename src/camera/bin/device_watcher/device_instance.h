// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_INSTANCE_H_
#define SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_INSTANCE_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/status.h>

// Represents a launched device process.
class DeviceInstance {
 public:
  DeviceInstance();
  static fit::result<std::unique_ptr<DeviceInstance>, zx_status_t> Create(
      const fuchsia::sys::LauncherPtr& launcher, fuchsia::hardware::camera::DeviceHandle camera,
      fit::closure on_component_unavailable);
  void OnCameraRequested(fidl::InterfaceRequest<fuchsia::camera3::Device> request);

 private:
  void OnServicesReady();
  void OnControllerRequested(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request);

  async::Loop loop_;
  fuchsia::hardware::camera::DevicePtr camera_;
  vfs::PseudoDir injected_services_dir_;
  std::shared_ptr<sys::ServiceDirectory> published_services_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  bool services_ready_ = false;
  std::vector<fidl::InterfaceRequest<fuchsia::camera3::Device>> pending_requests_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_INSTANCE_H_
