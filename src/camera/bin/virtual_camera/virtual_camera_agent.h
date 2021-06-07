// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_AGENT_H_
#define SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_AGENT_H_

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera/test/virtualcamera/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

namespace camera {

class VirtualCameraAgent : public fuchsia::hardware::camera::Device,
                           public fuchsia::camera::test::virtualcamera::VirtualCameraDevice {
 public:
  explicit VirtualCameraAgent(sys::ComponentContext* component_context);

  // fuchsia::hardware::camera::Device impl.
  void GetChannel(zx::channel channel) override;
  void GetChannel2(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> server_end) override;

  // fuchsia::camera::virtualcamera::VirtualCameraDevice impl.
  void AddToDeviceWatcher(AddToDeviceWatcherCallback callback) override;

 private:
  sys::ComponentContext* component_context_;
  fidl::Binding<fuchsia::hardware::camera::Device> hardware_device_binding_;
  fidl::InterfacePtr<fuchsia::camera::test::DeviceWatcherTester> device_watcher_tester_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_AGENT_H_
