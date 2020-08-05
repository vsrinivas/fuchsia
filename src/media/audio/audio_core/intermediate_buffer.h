// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_

#include <lib/fzl/owned-vmo-mapper.h>

#include <memory>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio {

// A buffer for writing temporary audio data. Each WriteLock() locks the start of the buffer,
// given by buffer().
class IntermediateBuffer : public WritableStream {
 public:
  IntermediateBuffer(const Format& output_format, uint32_t size_in_frames,
                     fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
                     AudioClock& ref_clock);

  void* buffer() const { return vmo_.start(); }
  size_t frame_count() const { return frame_count_; }

  // |media::audio::WritableStream|
  std::optional<Buffer> WriteLock(zx::time ref_time, int64_t frame, uint32_t frame_count) override;
  TimelineFunctionSnapshot ReferenceClockToFixed() const override;
  AudioClock& reference_clock() override { return audio_clock_; }

 private:
  fzl::OwnedVmoMapper vmo_;
  uint32_t frame_count_;
  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames_;
  AudioClock& audio_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_INTERMEDIATE_BUFFER_H_
