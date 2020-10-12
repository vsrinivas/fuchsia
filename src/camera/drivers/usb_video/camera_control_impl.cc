// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/camera_control_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <ddk/debug.h>

#include "src/camera/drivers/usb_video/usb_video_stream.h"

namespace camera {

void ControlImpl::OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame) {
  stream_->OnFrameAvailable(frame);
}

ControlImpl::ControlImpl(video::usb::UsbVideoStream* usb_video_stream,
                         fidl::InterfaceRequest<Control> control, async_dispatcher_t* dispatcher,
                         fit::closure on_connection_closed)
    : binding_(this, std::move(control), dispatcher), usb_video_stream_(usb_video_stream) {
  binding_.set_error_handler(
      [occ = std::move(on_connection_closed)](zx_status_t status) { occ(); });
}

void ControlImpl::GetFormats(uint32_t index, GetFormatsCallback callback) {
  if (formats_.size() == 0) {
    zx_status_t status = usb_video_stream_->GetFormats(formats_);
    if (status != ZX_OK) {
      callback(std::move(formats_), formats_.size(), status);
      return;
    }
  }

  size_t min_index = std::max((size_t)0, std::min((size_t)index, formats_.size() - 1));
  size_t max_index =
      std::min(min_index + fuchsia::camera::MAX_FORMATS_PER_RESPONSE - 1, formats_.size() - 1);

  callback(
      std::vector<fuchsia::camera::VideoFormat>(&(formats_)[min_index], &(formats_)[max_index + 1]),
      formats_.size(), ZX_OK);
}

void ControlImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  // This is just a conversion from the format internal to the device driver
  // to the FIDL DeviceInfo struct.
  const auto& usb_device_info = usb_video_stream_->GetDeviceInfo();
  fuchsia::camera::DeviceInfo camera_device_info;
  camera_device_info.vendor_name = usb_device_info.manufacturer;
  camera_device_info.vendor_id = usb_device_info.vendor_id;
  camera_device_info.product_name = usb_device_info.product_name;
  camera_device_info.product_id = usb_device_info.product_id;

  // TODO(fxbug.dev/3396): add more capabilities based on usb description
  camera_device_info.output_capabilities = fuchsia::camera::CAMERA_OUTPUT_STREAM;
  camera_device_info.max_stream_count = 1;
  callback(std::move(camera_device_info));
}

void ControlImpl::CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                               fuchsia::camera::FrameRate frame_rate,
                               fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                               zx::eventpair stream_token) {
  zx_status_t status = usb_video_stream_->CreateStream(std::move(buffer_collection), frame_rate);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set format. Closing channel.");
    binding_.Unbind();  // Close the channel on error.
    return;
  }

  stream_ = std::make_unique<StreamImpl>(*this, std::move(stream), std::move(stream_token));
}

void ControlImpl::StreamImpl::OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void ControlImpl::StreamImpl::Start() {
  zx_status_t status = owner_.usb_video_stream_->StartStreaming();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start. Closing channel.");
    binding_.Unbind();  // Close the channel on error.
  }
}

void ControlImpl::StreamImpl::Stop() {
  zx_status_t status = owner_.usb_video_stream_->StopStreaming();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to stop. Closing channel.");
    binding_.Unbind();  // Close the channel on error.
  }
}

void ControlImpl::StreamImpl::ReleaseFrame(uint32_t buffer_index) {
  zx_status_t status = owner_.usb_video_stream_->FrameRelease(buffer_index);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to release frame. Closing channel.");
    binding_.Unbind();  // Close the channel on error.
  }
}

void ControlImpl::ShutDownStream() {
  // This has the effect of cancelling the wait, deleting the
  // stream_token, and unbinding from the stream channel.
  stream_ = nullptr;
}

ControlImpl::StreamImpl::~StreamImpl() {
  // Doesn't matter what this returns:
  (void)owner_.usb_video_stream_->StopStreaming();
}

ControlImpl::StreamImpl::StreamImpl(ControlImpl& owner,
                                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                                    zx::eventpair stream_token)
    : owner_(owner),
      binding_(this, std::move(stream)),
      stream_token_(std::move(stream_token)),
      // If not triggered by the token being closed, this waiter will be
      // cancelled by the destruction of this class, so the "this" pointer will
      // be valid as long as the waiter is around.
      stream_token_waiter_(stream_token_.get(), ZX_EVENTPAIR_PEER_CLOSED, 0, std::bind([this]() {
                             zxlogf(DEBUG,
                                    "ControlImpl::StreamImpl::StreamImpl - "
                                    "ZX_EVENTPAIR_PEER_CLOSED received, shutting down stream.\n");
                             // If the peer is closed, shut down the whole
                             // stream.
                             owner_.ShutDownStream();
                             // We just deleted ourselves. Don't do anything
                             // else.
                           })) {
  zx_status_t status = stream_token_waiter_.Begin(async_get_default_dispatcher());
  // The waiter, dispatcher and token are known to be valid, so this should
  // never fail.
  FX_CHECK(status == ZX_OK);
  binding_.set_error_handler(
      [this](zx_status_t status) { owner_.usb_video_stream_->DeactivateVideoBuffer(); });
}

}  // namespace camera
