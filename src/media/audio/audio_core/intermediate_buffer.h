// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_

#include <memory>

#include "src/media/audio/audio_core/stream.h"

namespace media::audio {

class IntermediateBuffer : public Stream {
 public:
  IntermediateBuffer(const Format& output_format, uint32_t size_in_frames,
                     TimelineFunction reference_clock_to_fractional_frames);

  void* buffer() const { return buffer_.get(); }
  size_t frame_count() const { return frame_count_; }

  // |media::audio::Stream|
  std::optional<Buffer> LockBuffer(zx::time ref_time, int64_t frame, uint32_t frame_count) override;
  void UnlockBuffer(bool release_buffer) override {}
  void Trim(zx::time trim) override {}
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;

 private:
  std::unique_ptr<uint8_t[]> buffer_;
  uint32_t frame_count_;
  TimelineFunction reference_clock_to_fractional_frames_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_
