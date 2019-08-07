// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/eventpair.h>

#include <fbl/unique_ptr.h>

namespace video {
namespace usb {
class UsbVideoStream;
}
}  // namespace video

namespace camera {

class ControlImpl : public fuchsia::camera::Control {
 public:
  ControlImpl(video::usb::UsbVideoStream* usb_video_stream, fidl::InterfaceRequest<Control> control,
              async_dispatcher_t* dispatcher, fit::closure on_connection_closed);

  // Sent by the driver to the client when a frame is available for processing,
  // or an error occurred.
  void OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame);

 private:
  // Get the available format types for this device
  void GetFormats(uint32_t index, GetFormatsCallback callback) override;

  // Get the vendor and product information for this device
  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  // Sent by the client to indicate desired stream characteristics.
  // If setting the format is successful, the stream request will be honored.
  // The |stream_token| allows the camera manager to retain a handle to the
  // stream that was created, so it can signal the camera driver to kill the
  // stream if needed.
  void CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fuchsia::camera::FrameRate frame_rate,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                    zx::eventpair stream_token) override;

  void ShutDownStream();

  class StreamImpl : public fuchsia::camera::Stream {
   public:
    StreamImpl(ControlImpl& owner, fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
               zx::eventpair stream_token);

    ~StreamImpl();

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
    ControlImpl& owner_;
    fidl::Binding<Stream> binding_;
    zx::eventpair stream_token_;
    async::Wait stream_token_waiter_;
  };

  fbl::unique_ptr<StreamImpl> stream_;

  std::vector<fuchsia::camera::VideoFormat> formats_;

  ControlImpl(const ControlImpl&) = delete;
  ControlImpl& operator=(const ControlImpl&) = delete;

  fidl::Binding<Control> binding_;

  video::usb::UsbVideoStream* usb_video_stream_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_CAMERA_CONTROL_IMPL_H_
