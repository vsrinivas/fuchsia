// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_FIDL_H_
#define GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_FIDL_H_

#include <fbl/unique_ptr.h>
#include <fuchsia/camera/driver/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/media/timeline/timeline_function.h>

#include "garnet/lib/media/camera/simple_camera_lib/buffer.h"

namespace simple_camera {

// ColorSource steps through hue at a constant rate in HSV colorspace,
// with saturation and value remaining constant. An RGB color is written to
// a buffer provided.
class ColorSource {
 public:
  // Write the next color in the progression to the buffer.
  void WriteToBuffer(Buffer* buffer);

 private:
  void hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b);

  uint32_t frame_color_ = 0x80;
  static constexpr uint32_t kFrameColorInc = 0x01;
  static constexpr uint32_t kMaxFrameColor = 0x600;
};

class FakeControlImpl : public fuchsia::camera::driver::Control {
 public:
  FakeControlImpl(fidl::InterfaceRequest<Control> control,
                  async_dispatcher_t* dispatcher,
                  fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(
      const fuchsia::camera::driver::FrameAvailableEvent& frame);

  // Frame streaming stopped
  void Stopped();

  void PostNextCaptureTask();

 private:
  // Get the available format types for this device
  void GetFormats(uint32_t index, GetFormatsCallback callback) override;

  // Sent by the client to indicate desired stream characteristics.
  // If setting the format is successful, the stream request will be honored.
  void SetFormat(
      fuchsia::camera::driver::VideoFormat format,
      fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream,
      fidl::InterfaceRequest<fuchsia::camera::driver::StreamEvents> events,
      SetFormatCallback callback) override;

 private:
  class FakeStreamEventsImpl : public fuchsia::camera::driver::StreamEvents {
   public:
    FakeStreamEventsImpl(
        fidl::InterfaceRequest<fuchsia::camera::driver::StreamEvents> stream)
        : binding_(this, fbl::move(stream)) {}

    // Sent by the driver to the client when a frame is available for
    // processing, or an error occurred.
    void OnFrameAvailable(
        const fuchsia::camera::driver::FrameAvailableEvent& frame);

    // Frame streaming stopped
    void Stopped();

   private:
    fidl::Binding<StreamEvents> binding_;
  };

  class FakeStreamImpl : public fuchsia::camera::driver::Stream {
   public:
    FakeStreamImpl(
        FakeControlImpl& owner,
        fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream);

    // Set buffer storage used by camera capture
    void SetBuffer(::zx::vmo buffer, SetBufferCallback callback) override;

    // Starts the streaming of frames.
    void Start(StartCallback callback) override;

    // Stops the streaming of frames.
    void Stop(StopCallback callback) override;

    // Unlocks the specified frame, allowing the driver to reuse the memory.
    void ReleaseFrame(uint64_t data_offset,
                      ReleaseFrameCallback callback) override;

   private:
    FakeControlImpl& owner_;
    fidl::Binding<Stream> binding_;
  };

  fbl::unique_ptr<FakeStreamEventsImpl> stream_events_;
  fbl::unique_ptr<FakeStreamImpl> stream_;

  FakeControlImpl(const FakeControlImpl&) = delete;
  FakeControlImpl& operator=(const FakeControlImpl&) = delete;

  void SignalBufferFilled(uint32_t index);

  // Checks which buffer can be written to,
  // writes it then signals it ready
  // sleeps until next cycle
  void ProduceFrame();

  fidl::Binding<Control> binding_;

  static constexpr uint32_t kMinNumberOfBuffers = 2;
  static constexpr uint32_t kFramesOfDelay = 2;
  ColorSource color_source_;
  fuchsia::camera::driver::VideoFormat format_;
  uint64_t max_frame_size_ = 0;
  uint64_t frame_count_ = 0;
  std::vector<std::unique_ptr<Buffer>> buffers_;
  media::TimelineFunction frame_to_timestamp_;
  async::TaskClosureMethod<FakeControlImpl, &FakeControlImpl::ProduceFrame>
      task_{this};
};

}  // namespace simple_camera

#endif  // GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_FIDL_H_
