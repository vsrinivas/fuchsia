// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/intermediate_buffer.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/clock_reference.h"

namespace media::audio {

IntermediateBuffer::IntermediateBuffer(
    const Format& format, uint32_t size_in_frames,
    fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
    ClockReference ref_clock)
    : WritableStream(format),
      frame_count_(size_in_frames),
      reference_clock_to_fractional_frames_(reference_clock_to_fractional_frames),
      reference_clock_(ref_clock) {
  zx_status_t status =
      vmo_.CreateAndMap(frame_count_ * format.bytes_per_frame(), "intermediate-buffer");
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to create intermediate buffer VMO";
  }
}

std::optional<WritableStream::Buffer> IntermediateBuffer::WriteLock(zx::time ref_time,
                                                                    int64_t frame,
                                                                    uint32_t frame_count) {
  auto clamped_length = std::min<uint32_t>(frame_count, frame_count_);
  return std::make_optional<WritableStream::Buffer>(frame, clamped_length, vmo_.start());
}

WritableStream::TimelineFunctionSnapshot IntermediateBuffer::ReferenceClockToFractionalFrames()
    const {
  auto [timeline_function, generation] = reference_clock_to_fractional_frames_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

}  // namespace media::audio
