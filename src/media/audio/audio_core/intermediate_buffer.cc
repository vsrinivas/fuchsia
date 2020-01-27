// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/intermediate_buffer.h"

namespace media::audio {

IntermediateBuffer::IntermediateBuffer(const Format& format, uint32_t size_in_frames,
                                       TimelineFunction reference_clock_to_fractional_frames)
    : Stream(format),
      frame_count_(size_in_frames),
      reference_clock_to_fractional_frames_(reference_clock_to_fractional_frames) {
  buffer_ = std::make_unique<uint8_t[]>(frame_count_ * format.bytes_per_frame());
}

std::optional<Stream::Buffer> IntermediateBuffer::LockBuffer(zx::time ref_time, int64_t frame,
                                                             uint32_t frame_count) {
  auto clamped_length = std::min<uint32_t>(frame_count, frame_count_);
  return Stream::Buffer(frame, clamped_length, buffer_.get(), true);
}

Stream::TimelineFunctionSnapshot IntermediateBuffer::ReferenceClockToFractionalFrames() const {
  return {
      .timeline_function = reference_clock_to_fractional_frames_,
      .generation = kInvalidGenerationId + 1,
  };
}

}  // namespace media::audio
