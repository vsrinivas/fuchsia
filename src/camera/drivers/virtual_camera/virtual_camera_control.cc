// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_camera_control.h"

#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto TAG = "virtual_camera";

static const char* kVirtualCameraVendorName = "Google Inc.";
static const char* kVirtualCameraProductName = "Fuchsia Virtual Camera";
static constexpr uint32_t kFakeImageWidth = 640;
static constexpr uint32_t kFakeImageHeight = 480;
static constexpr uint32_t kFakeImageBytesPerPixel = 4;
static constexpr uint32_t kFakeImageFps = 30;
static constexpr uint64_t kNanosecondsPerSecond = 1e9;

void VirtualCameraControlImpl::OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame) {
  stream_->OnFrameAvailable(frame);
}

VirtualCameraControlImpl::VirtualCameraControlImpl(fidl::InterfaceRequest<Control> control,
                                                   async_dispatcher_t* dispatcher,
                                                   fit::closure on_connection_closed)
    : binding_(this, std::move(control), dispatcher) {
  binding_.set_error_handler(
      [occ = std::move(on_connection_closed)](zx_status_t /*status*/) { occ(); });
}

void VirtualCameraControlImpl::PostNextCaptureTask() {
  // Set the next frame time to be start + frame_count / frames per second.
  int64_t next_frame_time = frame_to_timestamp_.Apply(frame_count_++);
  FX_DCHECK(next_frame_time > 0) << "TimelineFunction gave negative result!";
  FX_DCHECK(next_frame_time != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";
  task_.PostForTime(async_get_default_dispatcher(), zx::time(next_frame_time));
  FX_VLOGS(4) << "VirtualCameraSource: setting next frame to: " << next_frame_time << "   "
              << next_frame_time - (static_cast<int64_t>(zx_clock_get_monotonic()))
              << " nsec from now";
}

// Checks which buffer can be written to,
// writes it, then signals it ready.
// Then sleeps until next cycle.
void VirtualCameraControlImpl::ProduceFrame() {
  fuchsia::camera::FrameAvailableEvent event = {};
  // For realism, give the frame a timestamp that is kFramesOfDelay frames
  // in the past:
  event.metadata.timestamp = frame_to_timestamp_.Apply(frame_count_ - kFramesOfDelay);
  FX_DCHECK(event.metadata.timestamp) << "TimelineFunction gave negative result!";
  FX_DCHECK(event.metadata.timestamp != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";

  // As per the camera driver spec, we always send an OnFrameAvailable message,
  // even if there is an error.
  auto buffer = buffers_.LockBufferForWrite();
  if (!buffer) {
    FX_LOGST(ERROR, TAG) << "no available frames, dropping frame #" << frame_count_;
    event.frame_status = fuchsia::camera::FrameStatus::ERROR_BUFFER_FULL;
  } else {  // Got a buffer.  Fill it with color:
    color_source_.FillARGB(buffer->virtual_address(), buffer->size());
    event.buffer_id = buffer->ReleaseWriteLockAndGetIndex();
  }

  OnFrameAvailable(event);
  // Schedule next frame:
  PostNextCaptureTask();
}

void VirtualCameraControlImpl::GetFormats(uint32_t /*index*/, GetFormatsCallback callback) {
  std::vector<fuchsia::camera::VideoFormat> formats;

  fuchsia::camera::VideoFormat format = {
      .format =
          {
              .width = kFakeImageWidth,
              .height = kFakeImageHeight,
              .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::BGRA32},
          },
      .rate = {.frames_per_sec_numerator = kFakeImageFps, .frames_per_sec_denominator = 1}};
  format.format.planes[0].bytes_per_row = kFakeImageBytesPerPixel * kFakeImageWidth;

  formats.push_back(format);
  callback(std::move(formats), 1, ZX_OK);
}

void VirtualCameraControlImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  fuchsia::camera::DeviceInfo camera_device_info;
  camera_device_info.vendor_name = kVirtualCameraVendorName;
  camera_device_info.product_name = kVirtualCameraProductName;
  camera_device_info.output_capabilities = fuchsia::camera::CAMERA_OUTPUT_STREAM;
  camera_device_info.max_stream_count = 1;
  callback(std::move(camera_device_info));
}

void VirtualCameraControlImpl::CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                                            fuchsia::camera::FrameRate frame_rate,
                                            fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                                            zx::eventpair stream_token) {
  rate_ = frame_rate;

  buffers_.Init(buffer_collection.vmos.data(), buffer_collection.buffer_count);
  buffers_.MapVmos();

  stream_ = std::make_unique<VirtualCameraStreamImpl>(*this, std::move(stream));
  stream_token_ = std::move(stream_token);
  // If not triggered by the token being closed, this waiter will be cancelled
  // by the destruction of this class, so the "this" pointer will be valid as
  // long as the waiter is around.
  stream_token_waiter_ = std::make_unique<async::Wait>(
      stream_token_.get(), ZX_EVENTPAIR_PEER_CLOSED, 0, std::bind([this]() {
        stream_->Stop();
        stream_ = nullptr;
        stream_token_.reset();
        stream_token_waiter_ = nullptr;
      }));

  zx_status_t status = stream_token_waiter_->Begin(async_get_default_dispatcher());
  // The waiter, dispatcher and token are known to be valid, so this should
  // never fail.
  FX_CHECK(status == ZX_OK);
}

void VirtualCameraControlImpl::VirtualCameraStreamImpl::OnFrameAvailable(
    const fuchsia::camera::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void VirtualCameraControlImpl::VirtualCameraStreamImpl::Start() {
  // Set a timeline function to convert from framecount to monotonic time.
  // The start time is now, the start frame number is 0, and the
  // conversion function from frame to time is:
  // frames_per_sec_denominator * 1e9 * num_frames) / frames_per_sec_numerator
  owner_.frame_to_timestamp_ = media::TimelineFunction(
      zx_clock_get_monotonic(), 0, owner_.rate_.frames_per_sec_denominator * kNanosecondsPerSecond,
      owner_.rate_.frames_per_sec_numerator);

  owner_.frame_count_ = 0;

  // Set the first time at which we will generate a frame:
  owner_.PostNextCaptureTask();
}

void VirtualCameraControlImpl::VirtualCameraStreamImpl::Stop() { owner_.task_.Cancel(); }

void VirtualCameraControlImpl::VirtualCameraStreamImpl::ReleaseFrame(uint32_t buffer_index) {
  owner_.buffers_.ReleaseBuffer(buffer_index);
}

VirtualCameraControlImpl::VirtualCameraStreamImpl::VirtualCameraStreamImpl(
    VirtualCameraControlImpl& owner, fidl::InterfaceRequest<fuchsia::camera::Stream> stream)
    : owner_(owner), binding_(this, std::move(stream)) {
  binding_.set_error_handler([](zx_status_t status) {
    // Anything to do here?
  });
}

}  // namespace camera
