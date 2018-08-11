// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/fake-camera-fidl.h>
#include <garnet/lib/media/camera/simple_camera_lib/video_display.h>

#include <fcntl.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>

namespace simple_camera {

const bool use_fake_camera = false;

// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void VideoDisplay::BufferReleased(FencedBuffer* buffer) {
  zx_status_t driver_status;
  zx_status_t status = camera_client_->stream_->ReleaseFrame(
      buffer->vmo_offset(), &driver_status);
  if (status != ZX_OK || driver_status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't release frame (status " << status << " : "
                   << driver_status << ")";
    on_shut_down_callback_();
  }
}

// We allow the incoming stream to reserve a write lock on a buffer
// it is writing to.  Reserving this buffer signals that it will be the latest
// buffer to be displayed. In other words, no buffer locked after this buffer
// will be displayed before this buffer.
// If the incoming buffer already filled, the driver could just call
// IncomingBufferFilled(), which will make sure the buffer is reserved first.
zx_status_t VideoDisplay::ReserveIncomingBuffer(FencedBuffer* buffer,
                                                uint64_t capture_time_ns) {
  if (nullptr == buffer) {
    FXL_LOG(ERROR) << "Invalid input buffer";
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t buffer_index = buffer->index();
  FXL_VLOG(4) << "Reserving incoming Buffer " << buffer_index;

  // check that no fences are set
  if (!buffer->IsAvailable()) {
    FXL_LOG(ERROR) << "Attempting to Reserve buffer " << buffer_index
                   << " which is marked unavailable.";
    return ZX_ERR_BAD_STATE;
  }

  uint64_t pres_time = frame_scheduler_.GetPresentationTimeNs(capture_time_ns);

  zx::event acquire_fence, release_fence;
  // TODO(garratt): these are supposed to be fire and forget:
  buffer->DuplicateAcquireFence(&acquire_fence);
  buffer->DuplicateReleaseFence(&release_fence);
  fidl::VectorPtr<zx::event> acquire_fences;
  acquire_fences.push_back(std::move(acquire_fence));
  fidl::VectorPtr<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));
  FXL_VLOG(4) << "presenting Buffer " << buffer_index << " at " << pres_time;

  image_pipe_->PresentImage(
      buffer_index, pres_time, std::move(acquire_fences),
      std::move(release_fences),
      [this, pres_time](const fuchsia::images::PresentationInfo& info) {
        this->frame_scheduler_.OnFramePresented(
            info.presentation_time, info.presentation_interval, pres_time);
      });
  return ZX_OK;
}

// When an incoming buffer is filled, VideoDisplay releases the acquire fence
zx_status_t VideoDisplay::IncomingBufferFilled(
    const fuchsia::camera::driver::FrameAvailableEvent& frame) {
  FencedBuffer* buffer;
  if (frame.frame_status != fuchsia::camera::driver::FrameStatus::OK) {
    FXL_LOG(ERROR) << "Error set on incoming frame. Error: "
                   << static_cast<int>(frame.frame_status);
    return ZX_OK;  // no reason to stop the channel...
  }

  zx_status_t status = FindOrCreateBuffer(frame.frame_size, frame.frame_offset,
                                          &buffer, format_);
  if (ZX_OK != status) {
    FXL_LOG(ERROR) << "Failed to create a frame for the incoming buffer";
    // What can we do here? If we cannot display the frame, quality will
    // suffer...
    return status;
  }

  // Now we know that the buffer exists.
  // If we have not reserved the buffer, do so now. ReserveIncomingBuffer
  // will quietly return if the buffer is already reserved.
  if (frame.metadata.timestamp < 0) {
    FXL_LOG(ERROR) << "Frame has bad timestamp: " << frame.metadata.timestamp;
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint64_t capture_time_ns = frame.metadata.timestamp;
  status = ReserveIncomingBuffer(buffer, capture_time_ns);
  if (ZX_OK != status) {
    FXL_LOG(ERROR) << "Failed to reserve a frame for the incoming buffer";
    return status;
  }

  // Signal that the buffer is ready to be presented:
  buffer->Signal();

  return ZX_OK;
}

// This is a stand-in for some actual gralloc type service which would allocate
// the right type of memory for the application and return it as a vmo.
zx_status_t Gralloc(uint64_t buffer_size, uint32_t num_buffers,
                    zx::vmo* buffer_vmo) {
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  return zx::vmo::create(num_buffers * buffer_size, 0, buffer_vmo);
}

// This function is a stand-in for the fact that our formats are not
// standardized accross the platform.  This is an issue, we are tracking
// it as (MTWN-98).
fuchsia::images::PixelFormat ConvertFormat(
    fuchsia::camera::driver::PixelFormat driver_format) {
  switch (driver_format) {
    case fuchsia::camera::driver::PixelFormat::BGRA32:
      return fuchsia::images::PixelFormat::BGRA_8;
    case fuchsia::camera::driver::PixelFormat::YUY2:
      return fuchsia::images::PixelFormat::YUY2;
    case fuchsia::camera::driver::PixelFormat::NV12:
      return fuchsia::images::PixelFormat::NV12;
    default:
      FXL_DCHECK(false) << "Unsupported format!";
  }
  return fuchsia::images::PixelFormat::BGRA_8;
}

zx_status_t VideoDisplay::FindOrCreateBuffer(
    uint32_t frame_size, uint64_t vmo_offset, FencedBuffer** buffer,
    const fuchsia::camera::driver::VideoFormat& format) {
  if (buffer != nullptr) {
    *buffer = nullptr;
  }
  // If the buffer exists, return the pointer
  for (std::unique_ptr<FencedBuffer>& b : frame_buffers_) {
    // TODO(garratt): For some cameras, the frame size changes.  Debug this
    // in the UVC driver.
    if (b->vmo_offset() == vmo_offset && b->size() >= frame_size) {
      if (nullptr != buffer) {
        *buffer = b.get();
      }
      return ZX_OK;
    }
  }
  // Buffer does not exist, make a new one!
  last_buffer_index_++;
  FXL_VLOG(4) << "Creating buffer " << last_buffer_index_;
  // TODO(garratt): change back to frame_size when we fix the fact that they are
  // changing...
  std::unique_ptr<FencedBuffer> b = FencedBuffer::Create(
      max_frame_size_, vmo_, vmo_offset, last_buffer_index_);
  if (b == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  // Set release fence callback so we know when a frame is made available
  b->SetReleaseFenceHandler(
      [this](FencedBuffer* b) { this->BufferReleased(b); });
  b->Reset();
  if (buffer != nullptr) {
    *buffer = b.get();
  }

  // Now add that buffer to the image pipe:
  FXL_VLOG(4) << "Creating ImageInfo ";
  // auto image_info = fuchsia::images::ImageInfo::New();
  fuchsia::images::ImageInfo image_info;
  image_info.stride = format.stride;
  image_info.tiling = fuchsia::images::Tiling::LINEAR;
  image_info.width = format.width;
  image_info.height = format.height;

  // To make things look like a webcam application, mirror left-right.
  image_info.transform = fuchsia::images::Transform::FLIP_HORIZONTAL;

  zx::vmo vmo;
  zx_status_t status = b->DuplicateVmoWithoutWrite(&vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate VMO from buffer";
    return status;
  }

  image_info.pixel_format = ConvertFormat(format.pixel_format);
  image_pipe_->AddImage(b->index(), image_info, std::move(vmo),
                        fuchsia::images::MemoryType::HOST_MEMORY, vmo_offset);

  frame_buffers_.push_back(std::move(b));
  return ZX_OK;
}

zx_status_t VideoDisplay::OpenCamera(int dev_id) {
  std::string dev_path = fxl::StringPrintf("/dev/class/camera/%03u", dev_id);
  fxl::UniqueFD dev_node(::open(dev_path.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FXL_LOG(ERROR) << "Client::Open failed to open device node at \""
                   << dev_path << "\". (" << strerror(errno) << " : " << errno
                   << ")";
    return ZX_ERR_IO;
  }

  zx::channel channel;
  ssize_t res =
      ioctl_camera_get_channel(dev_node.get(), channel.reset_and_get_address());
  if (res < 0) {
    FXL_LOG(ERROR) << "Failed to obtain channel (res " << res << ")";
    return static_cast<zx_status_t>(res);
  }

  camera_client_->control_.Bind(std::move(channel));

  return ZX_OK;
}

zx_status_t VideoDisplay::OpenFakeCamera() {
  // CameraStream FIDL interface
  static fbl::unique_ptr<simple_camera::FakeControlImpl>
      fake_camera_control_server_ = nullptr;
  // Loop used to run the FIDL server
  static fbl::unique_ptr<async::Loop> fidl_dispatch_loop_;

  FXL_LOG(INFO) << "Opening Fake Camera";

  if (fake_camera_control_server_ != nullptr) {
    FXL_LOG(ERROR) << "Camera Control already running";
    // TODO(CAM-XXX): support multiple concurrent clients.
    return ZX_ERR_ACCESS_DENIED;
  }

  if (fidl_dispatch_loop_ == nullptr) {
    fidl_dispatch_loop_ =
        fbl::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    fidl_dispatch_loop_->StartThread();
  }

  fidl::InterfaceHandle<fuchsia::camera::driver::Control> control_handle;
  fidl::InterfaceRequest<fuchsia::camera::driver::Control> control_interface =
      control_handle.NewRequest();

  if (control_interface.is_valid()) {
    FXL_LOG(INFO) << "Starting Fake Camera Server";
    fake_camera_control_server_ =
        fbl::make_unique<simple_camera::FakeControlImpl>(
            fbl::move(control_interface), fidl_dispatch_loop_->dispatcher(),
            [] {
              FXL_LOG(INFO) << "Deleting Fake Camera Server";
              fake_camera_control_server_.reset();
            });

    FXL_LOG(INFO) << "Binding camera_control_ to control_handle";
    camera_client_->control_.Bind(control_handle.TakeChannel());

    return ZX_OK;
  } else {
    return ZX_ERR_NO_RESOURCES;
  }
}

zx_status_t VideoDisplay::ConnectToCamera(
    uint32_t camera_id,
    ::fidl::InterfaceHandle<::fuchsia::images::ImagePipe> image_pipe,
    OnShutdownCallback callback) {
  if (!callback) {
    return ZX_ERR_INVALID_ARGS;
  }
  on_shut_down_callback_ = std::move(callback);

  image_pipe_ = image_pipe.Bind();
  image_pipe_.set_error_handler([this] {
    // Deal with image_pipe_ issues
    on_shut_down_callback_();
  });

  // Create the FIDL interface and bind events
  camera_client_ = std::make_unique<CameraClient>();

  camera_client_->events_.events().OnFrameAvailable =
      [video_display =
           this](fuchsia::camera::driver::FrameAvailableEvent frame) {
        video_display->IncomingBufferFilled(frame);
      };

  camera_client_->events_.events().Stopped = []() {
    FXL_LOG(INFO) << "Received Stopped Event";
  };

  camera_client_->events_.set_error_handler(
      [this] { on_shut_down_callback_(); });

  // Open a connection to the Camera
  zx_status_t driver_status;
  zx_status_t status =
      use_fake_camera ? OpenFakeCamera() : OpenCamera(camera_id);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't open camera client (status " << status << ")";
    goto error;
  }

  // Figure out a format
  {
    std::vector<fuchsia::camera::driver::VideoFormat> formats;
    zx_status_t driver_status;
    uint32_t total_format_count;
    uint32_t format_index = 0;
    do {
      fidl::VectorPtr<fuchsia::camera::driver::VideoFormat> formats_ptr;
      status = camera_client_->control_->GetFormats(
          format_index, &formats_ptr, &total_format_count, &driver_status);
      if (status != ZX_OK || driver_status != ZX_OK) {
        FXL_LOG(ERROR) << "Couldn't get camera formats (status " << status
                       << " : " << driver_status << ")";
        goto error;
      }
      const std::vector<fuchsia::camera::driver::VideoFormat>& call_formats =
          formats_ptr.get();
      for (auto&& f : call_formats) {
        formats.push_back(f);
      }
      format_index += call_formats.size();
    } while (formats.size() < total_format_count);

    FXL_LOG(INFO) << "Available formats: " << formats.size();
    for (int i = 0; i < (int)formats.size(); i++) {
      FXL_LOG(INFO) << "format[" << i << "] - width: " << formats[i].width
                    << ", height: " << formats[i].height
                    << ", stride: " << formats[i].stride
                    << ", bits_per_pixel: " << formats[i].bits_per_pixel;
    }

    format_ = formats[0];
  }

  // Allocate VMO buffer storage
  {
    uint32_t max_frame_size;

    status = camera_client_->control_->SetFormat(
        format_, camera_client_->stream_.NewRequest(),
        camera_client_->events_.NewRequest(), &max_frame_size, &driver_status);
    if (status != ZX_OK || driver_status != ZX_OK) {
      FXL_LOG(ERROR) << "Couldn't set camera format (status " << status << " : "
                     << driver_status << ")";
      goto error;
    }

    FXL_LOG(INFO) << "Allocating vmo buffer of size: "
                  << kNumberOfBuffers * max_frame_size;
    if (max_frame_size < format_.stride * format_.height) {
      FXL_LOG(INFO) << "SetFormat: max_frame_size: " << max_frame_size
                    << " < needed frame size: "
                    << format_.stride * format_.height;
      max_frame_size = format_.stride * format_.height;
    }

    max_frame_size_ = max_frame_size;
  }

  status = Gralloc(max_frame_size_, kNumberOfBuffers, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't create VMO buffer: " << status;
    status = ZX_ERR_INTERNAL;
    goto error;
  }

  // Pass the VMO storage to the camera driver
  {
    zx::vmo vmo_dup;
    status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to duplicate vmo (status: " << status << ").";
      status = ZX_ERR_INTERNAL;
      goto error;
    }

    status =
        camera_client_->stream_->SetBuffer(std::move(vmo_dup), &driver_status);
    if (status != ZX_OK || driver_status != ZX_OK) {
      FXL_LOG(ERROR) << "Couldn't set camera buffer (status " << status << " : "
                     << driver_status << ")";
      goto error;
    }
  }

  // Start streaming
  status = camera_client_->stream_->Start(&driver_status);
  if (status != ZX_OK || driver_status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't start camera (status " << status << " : "
                   << driver_status << ")";
    goto error;
  }

  FXL_LOG(INFO) << "Camera Client Initialization Successful!";

  return ZX_OK;

error:
  // Something went bad, release resources and return status
  DisconnectFromCamera();
  return status;
}

void VideoDisplay::DisconnectFromCamera() {
  image_pipe_.Unbind();
  camera_client_.reset();
  vmo_.reset();
}

}  // namespace simple_camera
