// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_NO_OP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_NO_OP_H_

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {

class NoOp : public Mixer {
 public:
  NoOp() : Mixer(0, 0) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) override;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_NO_OP_H_
