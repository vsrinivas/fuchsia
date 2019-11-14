// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_CAMERA_CONTROLLER_H_
#define SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_CAMERA_CONTROLLER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <vector>

namespace camera {

// Implements the camera HAL, with the ability to give bad configurations.
// When connections are made, they are stored internally, rather than bound to
// stream implementations, so stream connections will not be honored.
class FakeController : public fuchsia::camera2::hal::Controller {
 public:
  enum class GetConfigsFailureMode { VALID_CONFIGS, RETURNS_ERROR, EMPTY_VECTOR, INVALID_CONFIG };

  FakeController(std::vector<fuchsia::camera2::hal::Config> configs,
                 async_dispatcher_t* dispatcher);

  ~FakeController() { connections_.clear(); }

  // Produce the client side of a connection to this server. This can only be called once.
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> GetCameraConnection();

  // Set a failure type that will occur when GetConfigs() is called.
  void set_configs_failure(GetConfigsFailureMode failure_type) { configs_failure_ = failure_type; }

  // Verifies if the server side of the channel |client_side|
  // has been received by this controller.
  bool HasMatchingChannel(const zx::channel& client_side) const;

  // Get the number of streams that have been connected to this driver
  // by using the CreateStream function.
  uint32_t GetConnections() const { return connections_.size(); }

  // Fidl Functions:
  // Get a list of all available configurations which the camera driver supports.
  void GetConfigs(GetConfigsCallback callback) override;

  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    ::fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream) override;

  // Enable/Disable Streaming
  void EnableStreaming() override {}

  void DisableStreaming() override {}

  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  // Generate configurations that can be applied to the constructor:
  // Generates a pair of configurations, each with two streams:
  static std::vector<fuchsia::camera2::hal::Config> StandardConfigs();
  // Generates a pair of configurations with no streams:
  static std::vector<fuchsia::camera2::hal::Config> InvalidConfigs();

 private:
  struct CameraConnection {
    uint32_t config_index;
    uint32_t stream_index;
    uint32_t image_format_index;
    ::fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream;
  };

  std::vector<fuchsia::camera2::hal::Config> configs_;
  async_dispatcher_t* dispatcher_;
  std::vector<CameraConnection> connections_;
  fidl::Binding<fuchsia::camera2::hal::Controller> binding_{this};
  GetConfigsFailureMode configs_failure_ = GetConfigsFailureMode::VALID_CONFIGS;
};

}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_CAMERA_CONTROLLER_H_
