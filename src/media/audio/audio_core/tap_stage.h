// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TAP_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TAP_STAGE_H_

#include <memory>

#include "src/media/audio/audio_core/stream.h"

namespace media::audio {

// A |TapStage| reads stream buffers from an input |Stream| and copies them to a secondary
// |Stream|.
class TapStage : public Stream {
 public:
  // Creates a |TapStage| that returns buffers from |input| while copying their contents into |tap|.
  TapStage(std::shared_ptr<Stream> input, std::shared_ptr<Stream> tap);

  // |media::audio::Stream|
  std::optional<Stream::Buffer> LockBuffer(zx::time ref_time, int64_t frame,
                                           uint32_t frame_count) override;
  void UnlockBuffer(bool release_buffer) override { source_->UnlockBuffer(release_buffer); }
  void Trim(zx::time trim) override { source_->Trim(trim); }
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override {
    return source_->ReferenceClockToFractionalFrames();
  }

 private:
  std::shared_ptr<Stream> source_;
  std::shared_ptr<Stream> tap_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TAP_STAGE_H_
