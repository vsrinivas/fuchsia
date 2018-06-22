// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/fake_camera_source.h>
#include <zx/time.h>

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

void ColorSource::hsv_color(uint32_t index,
                            uint8_t* r,
                            uint8_t* g,
                            uint8_t* b) {
  uint8_t pos = index & 0xff;
  uint8_t neg = 0xff - (index & 0xff);
  uint8_t phase = (index >> 8) & 0x7;
  uint8_t phases[6] = {0xff, 0xff, neg, 0x00, 0x00, pos};
  *r = phases[(phase + 1) % countof(phases)];
  *g = phases[(phase + 5) % countof(phases)];
  *b = phases[(phase + 3) % countof(phases)];
}

uint64_t NSecPerFrame(const camera_video_format_t& format,
                      uint64_t num_frames = 1) {
  return (format.frames_per_sec_denominator * 1e9 * num_frames) /
         format.frames_per_sec_numerator;
}

zx_status_t FakeCameraSource::GetSupportedFormats(GetFormatCallback callback) {
  std::vector<camera_video_format_t> out_formats;
  camera_video_format_t format;
  format.pixel_format = RGB32;
  format.width = 640;
  format.height = 480;
  format.bits_per_pixel = 4;
  format.stride = 4 * 640;
  format.frames_per_sec_numerator = 30;
  format.frames_per_sec_denominator = 1;
  out_formats.push_back(format);
  callback(out_formats);
  return ZX_OK;
}

zx_status_t FakeCameraSource::SetFormat(const camera_video_format_t& format,
                                        SetFormatCallback callback) {
  format_ = format;
  // TODO(garratt): The method for calculating the size varies on the format.
  // Ideally, the format library should provide functions for calculating the
  // size.
  max_frame_size_ = format_.width * format_.height * format_.bits_per_pixel;
  callback(max_frame_size_);
  return ZX_OK;
}

zx_status_t FakeCameraSource::SetBuffer(const zx::vmo& vmo) {
  uint64_t buffer_size;
  vmo.get_size(&buffer_size);
  if (max_frame_size_ == 0 ||
      buffer_size < max_frame_size_ * kMinNumberOfBuffers) {
    FXL_LOG(ERROR) << "Insufficient space has been allocated";
    return ZX_ERR_NO_MEMORY;
  }
  uint64_t num_buffers = buffer_size / max_frame_size_;
  for (uint64_t i = 0; i < num_buffers; ++i) {
    std::unique_ptr<Buffer> buffer =
        Buffer::Create(max_frame_size_, vmo, max_frame_size_ * i);
    if (!buffer) {
      FXL_LOG(ERROR) << "Failed to create buffer.";
      return ZX_ERR_INTERNAL;
    }
    // Mark the buffer available.
    buffer->Reset();
    buffers_.push_back(std::move(buffer));
  }
  return ZX_OK;
}

zx_status_t FakeCameraSource::Start(FrameNotifyCallback callback) {
  if (!callback) {
    FXL_LOG(ERROR) << "callback is nullptr";
    return ZX_ERR_INVALID_ARGS;
  }
  notify_callback_ = fbl::move(callback);
  if (buffers_.empty()) {
    FXL_LOG(ERROR) << "Error: FakeCameraSource not initialized!";
    return ZX_ERR_BAD_STATE;
  }

  // Set a timeline function to convert from framecount to monotonic time.
  // The start time is now, the start frame number is 0, and the
  // conversion function from frame to time is:
  // frames_per_sec_denominator * 1e9 * num_frames) / frames_per_sec_numerator
  frame_to_timestamp_ =
      media::TimelineFunction(zx_clock_get(ZX_CLOCK_MONOTONIC), 0,
                              format_.frames_per_sec_denominator * 1e9,
                              format_.frames_per_sec_numerator);

  frame_count_ = 0;

  // Set the first time at which we will generate a frame:
  PostNextCaptureTask();
  return ZX_OK;
}

zx_status_t FakeCameraSource::Stop() {
  task_.Cancel();
  return ZX_OK;
}

void FakeCameraSource::SignalBufferFilled(uint32_t index) {
  FXL_VLOG(4) << "Signalling: " << index;
  if (index >= buffers_.size()) {
    FXL_LOG(ERROR) << "index out of range!";
    return;
  }
  if (notify_callback_) {
    camera_vb_frame_notify_t frame;
    frame.frame_size = buffers_[index]->size();
    frame.data_vb_offset = buffers_[index]->vmo_offset();
    // For realism, give the frame a timestamp that is kFramesOfDelay frames
    // in the past:
    frame.metadata.timestamp =
        frame_to_timestamp_.Apply(frame_count_ - kFramesOfDelay);
    FXL_DCHECK(frame.metadata.timestamp)
        << "TimelineFunction gave negative result!";
    FXL_DCHECK(frame.metadata.timestamp != media::TimelineRate::kOverflow)
        << "TimelineFunction gave negative result!";

    frame.error = camera_error::CAMERA_ERROR_NONE;
    // Inform the consumer that the frame is ready.
    notify_callback_(frame);
    buffers_[index]->Signal();
  }
}

void FakeCameraSource::PostNextCaptureTask() {
  // Set the next frame time to be start + frame_count / frames per second.
  int64_t next_frame_time = frame_to_timestamp_.Apply(frame_count_++);
  FXL_DCHECK(next_frame_time > 0) << "TimelineFunction gave negative result!";
  FXL_DCHECK(next_frame_time != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";
  task_.PostForTime(async_get_default(), zx::time(next_frame_time));
  FXL_VLOG(4) << "FakeCameraSource: setting next frame to: " << next_frame_time
              << "   "
              << next_frame_time - (int64_t)zx_clock_get(ZX_CLOCK_MONOTONIC)
              << " nsec from now";
}

zx_status_t FakeCameraSource::ReleaseFrame(uint64_t data_offset) {
  for (uint64_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i]->vmo_offset() == data_offset) {
      buffers_[i]->Reset();
      return ZX_OK;
    }
  }
  FXL_LOG(ERROR) << "data offset does not correspond to a frame!";
  return ZX_ERR_INVALID_ARGS;
}

// Checks which buffer can be written to,
// writes it, then signals it ready.
// Then sleeps until next cycle.
void FakeCameraSource::ProduceFrame() {
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

}  // namespace simple_camera
