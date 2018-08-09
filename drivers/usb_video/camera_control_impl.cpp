// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/usb_video/camera_control_impl.h"
#include "garnet/drivers/usb_video/usb-video-stream.h"

namespace camera {

void ControlImpl::OnFrameAvailable(
    const fuchsia::camera::driver::FrameAvailableEvent& frame) {
  stream_events_->OnFrameAvailable(frame);
}

void ControlImpl::Stopped() { stream_events_->Stopped(); }

ControlImpl::ControlImpl(video::usb::UsbVideoStream* usb_video_stream,
                         fidl::InterfaceRequest<Control> control,
                         async_dispatcher_t* dispatcher,
                         fit::closure on_connection_closed)
    : binding_(this, fbl::move(control), dispatcher),
      usb_video_stream_(usb_video_stream) {
  binding_.set_error_handler(fbl::move(on_connection_closed));
}

void ControlImpl::GetFormats(uint32_t index, GetFormatsCallback callback) {
  if (index == 0) {
    zx_status_t status = usb_video_stream_->GetFormats(formats_);
    if ((status == ZX_OK) &&
        (formats_->size() >
         fuchsia::camera::driver::MAX_FORMATS_PER_RESPONSE)) {
      fidl::VectorPtr<fuchsia::camera::driver::VideoFormat> formats;
      for (uint32_t i = 0;
           i < fuchsia::camera::driver::MAX_FORMATS_PER_RESPONSE; i++) {
        formats.push_back((*formats_)[i]);
      }
      callback(fbl::move(formats), formats_->size(), ZX_OK);
    } else {
      callback(fbl::move(formats_), formats_->size(), status);
    }
  } else {
    uint32_t formats_to_send =
        std::min(static_cast<uint32_t>(formats_->size() - index),
                 fuchsia::camera::driver::MAX_FORMATS_PER_RESPONSE);
    fidl::VectorPtr<fuchsia::camera::driver::VideoFormat> formats;
    if (index < formats_->size()) {
      for (uint32_t i = 0; i < formats_to_send; i++) {
        formats.push_back((*formats_)[index + i]);
      }

      callback(fbl::move(formats), formats_->size(), ZX_OK);
    } else {
      callback(fbl::move(formats), formats_->size(), ZX_ERR_INVALID_ARGS);
    }
  }
}

void ControlImpl::SetFormat(
    fuchsia::camera::driver::VideoFormat format,
    fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream,
    fidl::InterfaceRequest<fuchsia::camera::driver::StreamEvents> events,
    SetFormatCallback callback) {
  uint32_t max_frame_size;
  zx_status_t status = usb_video_stream_->SetFormat(format, &max_frame_size);

  if (status != ZX_OK) {
    callback(0, status);
    return;
  }

  stream_ = fbl::make_unique<StreamImpl>(*this, fbl::move(stream));
  stream_events_ = fbl::make_unique<StreamEventsImpl>(fbl::move(events));

  callback(max_frame_size, ZX_OK);
}

void ControlImpl::StreamEventsImpl::OnFrameAvailable(
    const fuchsia::camera::driver::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void ControlImpl::StreamEventsImpl::Stopped() { binding_.events().Stopped(); }

void ControlImpl::StreamImpl::SetBuffer(::zx::vmo buffer,
                                        SetBufferCallback callback) {
  zx_status_t result = owner_.usb_video_stream_->SetBuffer(fbl::move(buffer));
  callback(result);
}

void ControlImpl::StreamImpl::Start(StartCallback callback) {
  zx_status_t status = owner_.usb_video_stream_->StartStreaming();
  callback(status);
}

void ControlImpl::StreamImpl::Stop(StopCallback callback) {
  zx_status_t status = owner_.usb_video_stream_->StopStreaming();
  callback(status);
}

void ControlImpl::StreamImpl::ReleaseFrame(uint64_t data_offset,
                                           ReleaseFrameCallback callback) {
  zx_status_t status = owner_.usb_video_stream_->FrameRelease(data_offset);
  callback(status);
}

ControlImpl::StreamImpl::StreamImpl(
    ControlImpl& owner,
    fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream)
    : owner_(owner), binding_(this, fbl::move(stream)) {
  binding_.set_error_handler(
      [this] { owner_.usb_video_stream_->DeactivateVideoBuffer(); });
}

}  // namespace camera
