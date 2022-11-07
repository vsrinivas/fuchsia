// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_

#include "src/media/audio/audio_core/shared/mixer/mixer.h"

namespace media::audio::mixer {

class NoOp : public Mixer {
 public:
  NoOp() : Mixer(Fixed(0), Fixed(0), nullptr, Gain::Limits{}) {}

  void Mix(float* dest, int64_t dest_frames, int64_t* dest_offset, const void* source_void_ptr,
           int64_t source_frames, Fixed* source_offset_ptr, bool accumulate) override;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_
