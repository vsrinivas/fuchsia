// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_CONTROL_H_
#define GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_CONTROL_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/media/timeline/timeline_function.h>
#include <lib/zx/eventpair.h>

namespace virtual_camera {

// ColorSource steps through hue at a constant rate in HSV colorspace,
// with saturation and value remaining constant. An RGB color is written to
// a buffer provided.
class ColorSource {
 public:
  // Write the next color in the progression to the buffer.
  void FillARGB(void* start, size_t buffer_size);

 private:
  void hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b);

  uint32_t frame_color_ = 0x80;
  static constexpr uint32_t kFrameColorInc = 0x01;
  static constexpr uint32_t kMaxFrameColor = 0x600;
};

class VirtualCameraControlImpl : public fuchsia::camera::Control {
 public:
  VirtualCameraControlImpl(fidl::InterfaceRequest<Control> control,
                           async_dispatcher_t* dispatcher,
                           fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame);

  void PostNextCaptureTask();

 private:
  // Get the available format types for this device
  void GetFormats(uint32_t index, GetFormatsCallback callback) override;

  // Sent by the client to indicate desired stream characteristics.
  // If setting the format is successful, the stream request will be honored.
  void CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fuchsia::camera::FrameRate frame_rate,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                    zx::eventpair stream_token) override;

  // Get the vendor and product information about the device.
  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  class VirtualCameraStreamImpl : public fuchsia::camera::Stream {
   public:
    VirtualCameraStreamImpl(
        VirtualCameraControlImpl& owner,
        fidl::InterfaceRequest<fuchsia::camera::Stream> stream);

    // Starts the streaming of frames.
    void Start() override;

    // Stops the streaming of frames.
    void Stop() override;

    // Unlocks the specified frame, allowing the driver to reuse the memory.
    void ReleaseFrame(uint32_t buffer_index) override;

    // Sent by the driver to the client when a frame is available for
    // processing, or an error occurred.
    void OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame);

   private:
    VirtualCameraControlImpl& owner_;
    fidl::Binding<Stream> binding_;
  };

  std::unique_ptr<VirtualCameraStreamImpl> stream_;
  zx::eventpair stream_token_;
  std::unique_ptr<async::Wait> stream_token_waiter_;

  VirtualCameraControlImpl(const VirtualCameraControlImpl&) = delete;
  VirtualCameraControlImpl& operator=(const VirtualCameraControlImpl&) = delete;

  // Checks which buffer can be written to,
  // writes it then signals it ready
  // sleeps until next cycle
  void ProduceFrame();

  fidl::Binding<Control> binding_;

  static constexpr uint32_t kMinNumberOfBuffers = 2;
  static constexpr uint32_t kFramesOfDelay = 2;
  ColorSource color_source_;
  fuchsia::camera::FrameRate rate_;
  uint64_t frame_count_ = 0;

  fzl::VmoPool buffers_;
  media::TimelineFunction frame_to_timestamp_;
  async::TaskClosureMethod<VirtualCameraControlImpl,
                           &VirtualCameraControlImpl::ProduceFrame>
      task_{this};
};

}  // namespace virtual_camera

#endif  // GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_CONTROL_H_
