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

#include <set>

#include "src/camera/bin/virtual_camera/stream_storage.h"
#include "src/camera/bin/virtual_camera/virtual_camera_hal_controller.h"

namespace camera {

class VirtualCameraAgent : public fuchsia::hardware::camera::Device,
                           public fuchsia::camera::test::virtualcamera::VirtualCameraDevice {
 public:
  VirtualCameraAgent(sys::ComponentContext* component_context, StreamStorage& stream_storage);

  // fuchsia::hardware::camera::Device impl.
  void GetChannel(zx::channel channel) override;
  void GetChannel2(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> server_end) override;
  void GetDebugChannel(fidl::InterfaceRequest<fuchsia::camera2::debug::Debug> server_end) override;

  // fuchsia::camera::virtualcamera::VirtualCameraDevice impl.
  void AddToDeviceWatcher(AddToDeviceWatcherCallback callback) override;
  void AddStreamConfig(uint64_t index,
                       fuchsia::camera::test::virtualcamera::StreamConfig config) override;

 private:
  sys::ComponentContext* component_context_;
  StreamStorage& stream_storage_;
  fidl::Binding<fuchsia::hardware::camera::Device> hardware_device_binding_;
  fidl::InterfacePtr<fuchsia::camera::test::DeviceWatcherTester> device_watcher_tester_;
  std::set<std::unique_ptr<VirtualCameraHalController>> hal_controllers_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_AGENT_H_
