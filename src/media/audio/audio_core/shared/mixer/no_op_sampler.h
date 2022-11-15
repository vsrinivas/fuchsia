// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/processing/sampler.h"

namespace media::audio {

class NoOpSampler : public media_audio::Sampler {
 public:
  NoOpSampler() : media_audio::Sampler(media_audio::Fixed(0), media_audio::Fixed(0)) {}

  // Implements `media_audio::Sampler`.
  void EagerlyPrepare() final {}
  void Process(Source source, Dest dest, Gain gain, bool accumulate) final {}
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_NO_OP_H_
