// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/simple_camera/simple_camera_lib/video_display.h"

#include <fcntl.h>

#include <src/lib/files/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

namespace simple_camera {

// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void VideoDisplay::BufferReleased(uint32_t buffer_id) {
  camera_client_->stream_->ReleaseFrame(buffer_id);
}

// When an incoming buffer is filled, VideoDisplay releases the acquire fence
zx_status_t VideoDisplay::IncomingBufferFilled(const fuchsia::camera::FrameAvailableEvent& frame) {
  if (frame.frame_status != fuchsia::camera::FrameStatus::OK) {
    FX_LOGS(ERROR) << "Error set on incoming frame. Error: "
                   << static_cast<int>(frame.frame_status);
    return ZX_OK;  // no reason to stop the channel...
  }
  uint32_t buffer_id = frame.buffer_id;
  uint64_t capture_time_ns = frame.metadata.timestamp;
  uint64_t pres_time = frame_scheduler_.GetPresentationTimeNs(capture_time_ns);

  zx::event acquire_fence, release_fence;
  // TODO(garratt): these are supposed to be fire and forget:
  frame_buffers_[buffer_id]->DuplicateAcquireFence(&acquire_fence);
  frame_buffers_[buffer_id]->DuplicateReleaseFence(&release_fence);
  std::vector<zx::event> acquire_fences;
  acquire_fences.push_back(std::move(acquire_fence));
  std::vector<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));
  FX_VLOGS(4) << "presenting Buffer " << buffer_id << " at " << pres_time;

  image_pipe_->PresentImage(buffer_id + 1, pres_time, std::move(acquire_fences),
                            std::move(release_fences),
                            [this, pres_time](const fuchsia::images::PresentationInfo& info) {
                              this->frame_scheduler_.OnFramePresented(
                                  info.presentation_time, info.presentation_interval, pres_time);
                            });
  frame_buffers_[buffer_id]->Signal();
  return ZX_OK;
}

// This is a stand-in for some actual gralloc type service which would allocate
// the right type of memory for the application and return it as a vmo.
zx_status_t Gralloc(fuchsia::camera::VideoFormat format, uint32_t num_buffers,
                    fuchsia::sysmem::BufferCollectionInfo* buffer_collection) {
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  // Simple GetBufferSize.  Only valid for simple formats:
  size_t buffer_size =
      ROUNDUP(format.format.height * format.format.planes[0].bytes_per_row, PAGE_SIZE);
  buffer_collection->buffer_count = num_buffers;
  buffer_collection->vmo_size = buffer_size;
  buffer_collection->format.image = format.format;
  zx_status_t status;
  for (uint32_t i = 0; i < num_buffers; ++i) {
    status = zx::vmo::create(buffer_size, 0, &buffer_collection->vmos[i]);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to allocate Buffer Collection";
      return status;
    }
  }
  return ZX_OK;
}

// This function is a stand-in for the fact that our formats are not
// standardized accross the platform.  This is an issue, we are tracking
// it as (MTWN-98).
bool ConvertFormat(fuchsia::sysmem::PixelFormat driver_format,
                   fuchsia::images::PixelFormat* out_fmt) {
  switch (driver_format.type) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      *out_fmt = fuchsia::images::PixelFormat::BGRA_8;
      return true;
    case fuchsia::sysmem::PixelFormatType::YUY2:
      *out_fmt = fuchsia::images::PixelFormat::YUY2;
      return true;
    case fuchsia::sysmem::PixelFormatType::NV12:
      *out_fmt = fuchsia::images::PixelFormat::NV12;
      return true;
    default:
      return false;
  }
}

zx_status_t VideoDisplay::SetupBuffers(
    const fuchsia::sysmem::BufferCollectionInfo& buffer_collection) {
  fuchsia::images::ImageInfo image_info;
  image_info.stride = buffer_collection.format.image.planes[0].bytes_per_row;
  image_info.tiling = fuchsia::images::Tiling::LINEAR;
  image_info.width = buffer_collection.format.image.width;
  image_info.height = buffer_collection.format.image.height;

  // To make things look like a webcam application, mirror left-right.
  image_info.transform = fuchsia::images::Transform::FLIP_HORIZONTAL;

  if (!ConvertFormat(buffer_collection.format.image.pixel_format, &image_info.pixel_format)) {
    FX_CHECK(false) << "Unsupported format";
  }

  for (size_t id = 0; id < buffer_collection.buffer_count; ++id) {
    zx::vmo vmo_dup;
    zx_status_t status = buffer_collection.vmos[id].duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to duplicate vmo";
      return status;
    }
    image_pipe_->AddImage(id + 1, image_info, std::move(vmo_dup), 0, buffer_collection.vmo_size,
                          fuchsia::images::MemoryType::HOST_MEMORY);

    // Now create the fence for the buffer:
    std::unique_ptr<BufferFence> fence = BufferFence::Create(id);
    if (fence == nullptr) {
      return ZX_ERR_INTERNAL;
    }
    // Set release fence callback so we know when a frame is made available
    fence->SetReleaseFenceHandler(
        // TODO(garratt): This does not handle return value
        [this](BufferFence* fence) { this->BufferReleased(fence->index()); });
    fence->Reset();
    frame_buffers_.push_back(std::move(fence));
  }
  return ZX_OK;
}

// TODO(CAM-9): Clean up this function after major changes land.
zx_status_t VideoDisplay::ConnectToCamera(
    sys::ComponentContext* context, uint32_t camera_id,
    fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe, OnShutdownCallback callback) {
  if (!callback) {
    return ZX_ERR_INVALID_ARGS;
  }
  on_shut_down_callback_ = std::move(callback);

  image_pipe_ = image_pipe.Bind();
  image_pipe_.set_error_handler([this](zx_status_t /*status*/) {
    // Deal with image_pipe_ issues
    on_shut_down_callback_();
  });

  // Create the FIDL interface and bind events
  camera_client_ = std::make_unique<CameraClient>();

  camera_client_->stream_.events().OnFrameAvailable =
      [this](fuchsia::camera::FrameAvailableEvent frame) { IncomingBufferFilled(frame); };

  camera_client_->stream_.set_error_handler([this](zx_status_t /*status*/) {
    DisconnectFromCamera();
    on_shut_down_callback_();
  });

  // Open a connection to the Camera Manager
  context->svc()->Connect(camera_client_->manager_.NewRequest());

  zx_status_t status = ZX_OK;

  // Figure out a format
  std::vector<fuchsia::camera::VideoFormat> formats;
  {
    uint32_t total_format_count;
    uint32_t format_index = 0;
    do {
      std::vector<fuchsia::camera::VideoFormat> formats_ptr;
      status = camera_client_->manager_->GetFormats(camera_id, format_index, &formats_ptr,
                                                    &total_format_count);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Couldn't get camera formats";
        DisconnectFromCamera();
        return status;
      }
      for (auto&& f : formats_ptr) {
        formats.push_back(f);
      }
      format_index += formats_ptr.size();
    } while (formats.size() < total_format_count);

    FX_LOGS(INFO) << "Available formats: " << formats.size();
    for (size_t i = 0; i < formats.size(); i++) {
      FX_LOGS(INFO) << "format[" << i << "] - width: " << formats[i].format.width
                    << ", height: " << formats[i].format.height
                    << ", stride: " << formats[i].format.planes[0].bytes_per_row;
    }
  }
  uint32_t idx = 0;
  fuchsia::images::PixelFormat fmt;
  while (!ConvertFormat(formats[idx].format.pixel_format, &fmt)) {
    if (++idx == formats.size()) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  auto chosen_format = formats[idx];
  // Allocate VMO buffer storage
  {
    fuchsia::sysmem::BufferCollectionInfo buffer_collection;
    status = Gralloc(chosen_format, kNumberOfBuffers, &buffer_collection);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Couldn't allocate buffers";
      DisconnectFromCamera();
      return status;
    }

    status = SetupBuffers(buffer_collection);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Couldn't set up buffers";
      DisconnectFromCamera();
      return status;
    }

    // Create stream token.  The stream token is not very meaningful when
    // you have a direct connection to the driver, but this use case should
    // be disappearing soon anyway.  For now, we just hold on to the token.
    zx::eventpair driver_token;
    status = zx::eventpair::create(0, &stream_token_, &driver_token);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Couldn't create driver token";
      DisconnectFromCamera();
      return status;
    }

    fuchsia::camera::VideoStream request = {.camera_id = camera_id, .format = chosen_format};

    status = camera_client_->manager_->CreateStream(request, std::move(buffer_collection),
                                                    camera_client_->stream_.NewRequest(),
                                                    std::move(driver_token));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Couldn't set camera format";
      DisconnectFromCamera();
      return status;
    }
  }

  // Start streaming
  camera_client_->stream_->Start();

  return ZX_OK;
}

void VideoDisplay::DisconnectFromCamera() {
  image_pipe_.Unbind();
  camera_client_ = nullptr;
}

}  // namespace simple_camera
