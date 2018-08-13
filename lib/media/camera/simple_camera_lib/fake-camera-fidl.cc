// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>

#include "fake-camera-fidl.h"

namespace simple_camera {

void ColorSource::WriteToBuffer(Buffer* buffer) {
  if (!buffer) {
    FXL_LOG(ERROR) << "Must pass a valid buffer pointer";
    return;
  }
  uint8_t r, g, b;
  hsv_color(frame_color_, &r, &g, &b);
  FXL_VLOG(4) << "Filling with " << (int)r << " " << (int)g << " " << (int)b;
  buffer->FillARGB(r, g, b);
  frame_color_ += kFrameColorInc;
  if (frame_color_ > kMaxFrameColor) {
    frame_color_ -= kMaxFrameColor;
  }
}

void ColorSource::hsv_color(uint32_t index, uint8_t* r, uint8_t* g,
                            uint8_t* b) {
  uint8_t pos = index & 0xff;
  uint8_t neg = 0xff - (index & 0xff);
  uint8_t phase = (index >> 8) & 0x7;
  uint8_t phases[6] = {0xff, 0xff, neg, 0x00, 0x00, pos};
  *r = phases[(phase + 1) % countof(phases)];
  *g = phases[(phase + 5) % countof(phases)];
  *b = phases[(phase + 3) % countof(phases)];
}

void FakeControlImpl::OnFrameAvailable(
    const fuchsia::camera::driver::FrameAvailableEvent& frame) {
  stream_events_->OnFrameAvailable(frame);
}

void FakeControlImpl::Stopped() { stream_events_->Stopped(); }

FakeControlImpl::FakeControlImpl(fidl::InterfaceRequest<Control> control,
                                 async_dispatcher_t* dispatcher,
                                 fit::closure on_connection_closed)
    : binding_(this, fbl::move(control), dispatcher) {
  binding_.set_error_handler(fbl::move(on_connection_closed));
}

void FakeControlImpl::PostNextCaptureTask() {
  // Set the next frame time to be start + frame_count / frames per second.
  int64_t next_frame_time = frame_to_timestamp_.Apply(frame_count_++);
  FXL_DCHECK(next_frame_time > 0) << "TimelineFunction gave negative result!";
  FXL_DCHECK(next_frame_time != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";
  task_.PostForTime(async_get_default_dispatcher(), zx::time(next_frame_time));
  FXL_VLOG(4) << "FakeCameraSource: setting next frame to: " << next_frame_time
              << "   "
              << next_frame_time - (int64_t)zx_clock_get(ZX_CLOCK_MONOTONIC)
              << " nsec from now";
}

void FakeControlImpl::SignalBufferFilled(uint32_t index) {
  FXL_VLOG(4) << "Signalling: " << index;
  if (index >= buffers_.size()) {
    FXL_LOG(ERROR) << "index out of range!";
    return;
  }

  fuchsia::camera::driver::FrameAvailableEvent frame;
  frame.frame_size = buffers_[index]->size();
  frame.frame_offset = buffers_[index]->vmo_offset();
  // For realism, give the frame a timestamp that is kFramesOfDelay frames
  // in the past:
  frame.metadata.timestamp =
      frame_to_timestamp_.Apply(frame_count_ - kFramesOfDelay);
  FXL_DCHECK(frame.metadata.timestamp)
      << "TimelineFunction gave negative result!";
  FXL_DCHECK(frame.metadata.timestamp != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";

  frame.frame_status = fuchsia::camera::driver::FrameStatus::OK;
  // Inform the consumer that the frame is ready.
  OnFrameAvailable(frame);
  buffers_[index]->Signal();
}

// Checks which buffer can be written to,
// writes it, then signals it ready.
// Then sleeps until next cycle.
void FakeControlImpl::ProduceFrame() {
  for (uint64_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i]->IsAvailable()) {
      // Fill buffer.  Currently, just uses a rotating color.
      color_source_.WriteToBuffer(buffers_[i].get());
      SignalBufferFilled(i);
      break;
    }
  }
  // If no buffers are available, quietly fail to fill.
  // Schedule next frame:
  PostNextCaptureTask();
}

void FakeControlImpl::GetFormats(uint32_t index, GetFormatsCallback callback) {
  fidl::VectorPtr<fuchsia::camera::driver::VideoFormat> formats;

  fuchsia::camera::driver::VideoFormat format = {
      .format = {.pixel_format = {.type =
                                      fuchsia::sysmem::PixelFormatType::BGRA32},
                 .width = 640,
                 .height = 480,
                 // .bits_per_pixel = 4,
                 .bytes_per_row = 4 * 640},
      .rate = {.frames_per_sec_numerator = 30,
               .frames_per_sec_denominator = 1}};

  formats.push_back(format);
  callback(fbl::move(formats), 1, ZX_OK);
}

void FakeControlImpl::SetFormat(
    fuchsia::camera::driver::VideoFormat format,
    fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream,
    fidl::InterfaceRequest<fuchsia::camera::driver::StreamEvents> events,
    SetFormatCallback callback) {
  format_ = format;
  // TODO(garratt): The method for calculating the size varies on the format.
  // Ideally, the format library should provide functions for calculating the
  // size.
  max_frame_size_ = format_.format.height * format_.format.bytes_per_row;

  stream_ = fbl::make_unique<FakeStreamImpl>(*this, fbl::move(stream));
  stream_events_ = fbl::make_unique<FakeStreamEventsImpl>(fbl::move(events));

  callback(max_frame_size_, ZX_OK);
}

void FakeControlImpl::FakeStreamEventsImpl::OnFrameAvailable(
    const fuchsia::camera::driver::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void FakeControlImpl::FakeStreamEventsImpl::Stopped() {
  binding_.events().Stopped();
}

void FakeControlImpl::FakeStreamImpl::SetBuffer(::zx::vmo vmo,
                                                SetBufferCallback callback) {
  uint64_t buffer_size;
  vmo.get_size(&buffer_size);
  if (owner_.max_frame_size_ == 0 ||
      buffer_size < owner_.max_frame_size_ * kMinNumberOfBuffers) {
    FXL_LOG(ERROR) << "Insufficient space has been allocated";
    callback(ZX_ERR_NO_MEMORY);
    return;
  }
  uint64_t num_buffers = buffer_size / owner_.max_frame_size_;
  for (uint64_t i = 0; i < num_buffers; ++i) {
    std::unique_ptr<Buffer> buffer =
        Buffer::Create(owner_.max_frame_size_, vmo, owner_.max_frame_size_ * i);
    if (!buffer) {
      FXL_LOG(ERROR) << "Failed to create buffer.";
      callback(ZX_ERR_INTERNAL);
      return;
    }
    // Mark the buffer available.
    buffer->Reset();
    owner_.buffers_.push_back(std::move(buffer));
  }

  callback(ZX_OK);
}

void FakeControlImpl::FakeStreamImpl::Start(StartCallback callback) {
  if (owner_.buffers_.empty()) {
    FXL_LOG(ERROR) << "Error: FakeCameraSource not initialized!";
    callback(ZX_ERR_BAD_STATE);
    return;
  }

  // Set a timeline function to convert from framecount to monotonic time.
  // The start time is now, the start frame number is 0, and the
  // conversion function from frame to time is:
  // frames_per_sec_denominator * 1e9 * num_frames) / frames_per_sec_numerator
  owner_.frame_to_timestamp_ =
      media::TimelineFunction(zx_clock_get(ZX_CLOCK_MONOTONIC), 0,
                              owner_.format_.rate.frames_per_sec_denominator * 1e9,
                              owner_.format_.rate.frames_per_sec_numerator);

  owner_.frame_count_ = 0;

  // Set the first time at which we will generate a frame:
  owner_.PostNextCaptureTask();

  callback(ZX_OK);
}

void FakeControlImpl::FakeStreamImpl::Stop(StopCallback callback) {
  owner_.task_.Cancel();

  callback(ZX_OK);
}

void FakeControlImpl::FakeStreamImpl::ReleaseFrame(
    uint64_t data_offset, ReleaseFrameCallback callback) {
  for (uint64_t i = 0; i < owner_.buffers_.size(); ++i) {
    if (owner_.buffers_[i]->vmo_offset() == data_offset) {
      owner_.buffers_[i]->Reset();
      callback(ZX_OK);
      return;
    }
  }
  FXL_LOG(ERROR) << "data offset does not correspond to a frame!";
  callback(ZX_ERR_INVALID_ARGS);
  return;
}

FakeControlImpl::FakeStreamImpl::FakeStreamImpl(
    FakeControlImpl& owner,
    fidl::InterfaceRequest<fuchsia::camera::driver::Stream> stream)
    : owner_(owner), binding_(this, fbl::move(stream)) {
  binding_.set_error_handler([this] {
    // Anything to do here?
  });
}

}  // namespace simple_camera
