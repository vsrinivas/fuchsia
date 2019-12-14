// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA2_CONTROL_H_
#define SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA2_CONTROL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/eventpair.h>

#include "src/camera/lib/image_writer/color_source.h"

namespace camera {

class VirtualCamera2ControllerImpl : public fuchsia::camera2::hal::Controller {
 public:
  VirtualCamera2ControllerImpl(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
                               async_dispatcher_t* dispatcher, fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame);

  void PostNextCaptureTask();

 private:
  class VirtualCamera2StreamImpl : public fuchsia::camera2::Stream {
   public:
    VirtualCamera2StreamImpl(VirtualCamera2ControllerImpl& owner,
                             fidl::InterfaceRequest<fuchsia::camera2::Stream> stream);

    // Starts the streaming of frames.
    void Start() override;

    // Stops the streaming of frames.
    void Stop() override;

    // Unlocks the specified frame, allowing the driver to reuse the memory.
    void ReleaseFrame(uint32_t buffer_index) override;

    // Sent by the driver to the client when a frame is available for
    // processing, or an error occurred.
    void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame);

    void AcknowledgeFrameError() override {}
    // Data operations
    // This is used by clients to provide inputs for region of interest
    // selection.
    // Inputs are the x & y coordinates for the new bounding box.
    // For streams which do not support smart framing, this would
    // return an error.
    void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                             SetRegionOfInterestCallback callback) override {}

    // Change the image format of the stream. This is called when clients want
    // to dynamically change the resolution of the stream while the streaming is
    // is going on.
    void SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) override {}

    // Get the image formats that this stream supports.
    void GetImageFormats(GetImageFormatsCallback callback) override {}

   private:
    VirtualCamera2ControllerImpl& owner_;
    fidl::Binding<fuchsia::camera2::Stream> binding_;
  };

  static constexpr uint32_t kFramesOfDelay = 2;

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
                    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override;

  // Enable/Disable Streaming
  void EnableStreaming() override {}

  void DisableStreaming() override {}

  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  std::unique_ptr<VirtualCamera2StreamImpl> stream_;

  VirtualCamera2ControllerImpl(const VirtualCamera2ControllerImpl&) = delete;
  VirtualCamera2ControllerImpl& operator=(const VirtualCamera2ControllerImpl&) = delete;

  // Checks which buffer can be written to,
  // writes it then signals it ready
  // sleeps until next cycle
  void ProduceFrame();

  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;

  ColorSource color_source_;
  fuchsia::camera2::FrameRate rate_;
  uint64_t frame_count_ = 0;

  std::vector<fuchsia::camera2::hal::Config> configs_;
  fzl::VmoPool buffers_;
  media::TimelineFunction frame_to_timestamp_;
  async::TaskClosureMethod<VirtualCamera2ControllerImpl,
                           &VirtualCamera2ControllerImpl::ProduceFrame>
      task_{this};
};

inline const char* kVirtualCameraVendorName = "Google Inc.";
inline const char* kVirtualCameraProductName = "Fuchsia Virtual Camera";
}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA2_CONTROL_H_
