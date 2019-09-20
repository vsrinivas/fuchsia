// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/binding.h>

namespace camera {

namespace {
const char* kCameraVendorName = "Google Inc.";
const char* kCameraProductName = "Fuchsia Sherlock Camera";
}  // namespace

class ControllerImpl : public fuchsia::camera2::hal::Controller {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ControllerImpl);
  ControllerImpl(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
                 async_dispatcher_t* dispatcher, fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame);

 private:
  // Device FIDL implementation

  // Get a list of all available configurations which the camera driver supports.
  void GetConfigs(GetConfigsCallback callback) override;

  // Set a particular configuration and create the requested stream.
  // |config_index| : Configuration index from the vector which needs to be applied.
  // |stream_type| : Stream types (one of more of |CameraStreamTypes|)
  // |buffer_collection| : Buffer collections for the stream.
  // |stream| : Stream channel for the stream requested
  // |image_format_index| : Image format index which needs to be set up upon creation.
  // If there is already an active configuration which is different than the one
  // which is requested to be set, then the HAL will be closing all existing streams
  // and honor this new setup call.
  // If the new stream requested is already part of the existing running configuration
  // the HAL will just be creating this new stream while the other stream still exists as is.
  void CreateStream(uint32_t config_index, uint32_t stream_type, uint32_t image_format_index,
                    ::fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream) override{};

  // Enable/Disable Streaming
  void EnableStreaming() override {}

  void DisableStreaming() override {}

  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  // Helper APIs
  static std::vector<fuchsia::camera2::hal::Config> SherlockConfigs();

  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
