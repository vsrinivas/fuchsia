// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_
#define GARNET_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_

#include <fbl/unique_ptr.h>
#include <fuchsia/camera/driver/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

namespace video {
namespace usb {
class UsbVideoStream;
}
}  // namespace video

namespace camera {

class ControlImpl : public fuchsia::camera::driver::Control {
 public:
  ControlImpl(video::usb::UsbVideoStream* usb_video_stream,
              fidl::InterfaceRequest<Control> control,
              async_dispatcher_t* dispatcher,
              fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(
      const fuchsia::camera::driver::FrameAvailableEvent& frame);

  // Frame streaming stopped
  void Stopped();

 private:
  // Get the available format types for this device
  void GetFormats(GetFormatsCallback callback) override;

  // Sent by the client to indicate desired stream characteristics.
  // If setting the format is successful, the stream request will be honored.
  void SetFormat(
      fuchsia::camera::driver::VideoFormat format,
      fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream,
      fidl::InterfaceRequest<fuchsia::camera::driver::StreamEvents> events,
      SetFormatCallback callback) override;

 private:
  class StreamEventsImpl : public fuchsia::camera::driver::StreamEvents {
   public:
    StreamEventsImpl(
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

  class StreamImpl : public fuchsia::camera::driver::Stream {
   public:
    StreamImpl(ControlImpl& owner,
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
    ControlImpl& owner_;
    fidl::Binding<Stream> binding_;
  };

  fbl::unique_ptr<StreamEventsImpl> stream_events_;
  fbl::unique_ptr<StreamImpl> stream_;

  ControlImpl(const ControlImpl&) = delete;
  ControlImpl& operator=(const ControlImpl&) = delete;

  fidl::Binding<Control> binding_;

  video::usb::UsbVideoStream* usb_video_stream_;
};

}  // namespace camera

#endif  // GARNET_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_
