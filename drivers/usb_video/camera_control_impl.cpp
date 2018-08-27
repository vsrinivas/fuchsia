// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "garnet/drivers/usb_video/camera_control_impl.h"
#include "garnet/drivers/usb_video/usb-video-stream.h"

namespace camera {

void ControlImpl::OnFrameAvailable(
    const fuchsia::camera::FrameAvailableEvent& frame) {
  stream_->OnFrameAvailable(frame);
}

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
        (formats_->size() > fuchsia::camera::MAX_FORMATS_PER_RESPONSE)) {
      fidl::VectorPtr<fuchsia::camera::VideoFormat> formats;
      for (uint32_t i = 0; i < fuchsia::camera::MAX_FORMATS_PER_RESPONSE; i++) {
        formats.push_back((*formats_)[i]);
      }
      callback(fbl::move(formats), formats_->size(), ZX_OK);
    } else {
      callback(fbl::move(formats_), formats_->size(), status);
    }
  } else {
    uint32_t formats_to_send =
        std::min(static_cast<uint32_t>(formats_->size() - index),
                 fuchsia::camera::MAX_FORMATS_PER_RESPONSE);
    fidl::VectorPtr<fuchsia::camera::VideoFormat> formats;
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

void ControlImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  // This is just a conversion from the format internal to the device driver
  // to the FIDL DeviceInfo struct.
  const auto& usb_device_info = usb_video_stream_->GetDeviceInfo();
  fuchsia::camera::DeviceInfo camera_device_info;
  camera_device_info.vendor_name = usb_device_info.manufacturer;
  camera_device_info.vendor_id = usb_device_info.vendor_id;
  camera_device_info.product_name = usb_device_info.product_name;
  camera_device_info.product_id = usb_device_info.product_id;
  camera_device_info.serial_number = usb_device_info.serial_number;

  // TODO(CAM-11): add more capabilities based on usb description
  camera_device_info.output_capabilities =
      fuchsia::camera::CAMERA_OUTPUT_STREAM;
  camera_device_info.max_stream_count = 1;
  callback(std::move(camera_device_info));
}

void ControlImpl::CreateStream(
    fuchsia::sysmem::BufferCollectionInfo buffer_collection,
    fuchsia::camera::FrameRate frame_rate,
    fidl::InterfaceRequest<fuchsia::camera::Stream> stream) {
  zx_status_t status =
      usb_video_stream_->CreateStream(std::move(buffer_collection), frame_rate);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set format. Closing channel.\n");
    binding_.Unbind();  // Close the channel on error.
    return;
  }

  stream_ = fbl::make_unique<StreamImpl>(*this, fbl::move(stream));
}

void ControlImpl::StreamImpl::OnFrameAvailable(
    const fuchsia::camera::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void ControlImpl::StreamImpl::Start() {
  zx_status_t status = owner_.usb_video_stream_->StartStreaming();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start. Closing channel.\n");
    binding_.Unbind();  // Close the channel on error.
  }
}

void ControlImpl::StreamImpl::Stop() {
  zx_status_t status = owner_.usb_video_stream_->StopStreaming();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to stop. Closing channel.\n");
    binding_.Unbind();  // Close the channel on error.
  }
}

void ControlImpl::StreamImpl::ReleaseFrame(uint32_t buffer_index) {
  zx_status_t status = owner_.usb_video_stream_->FrameRelease(buffer_index);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to release frame. Closing channel.\n");
    binding_.Unbind();  // Close the channel on error.
  }
}

ControlImpl::StreamImpl::StreamImpl(
    ControlImpl& owner, fidl::InterfaceRequest<fuchsia::camera::Stream> stream)
    : owner_(owner), binding_(this, fbl::move(stream)) {
  binding_.set_error_handler(
      [this] { owner_.usb_video_stream_->DeactivateVideoBuffer(); });
}

}  // namespace camera
