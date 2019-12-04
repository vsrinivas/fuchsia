// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FRAMES_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FRAMES_H_

#include <ffl/fixed.h>

#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio {

template <typename Integer>
using FractionalFrames = ffl::Fixed<Integer, kPtsFractionalBits>;

static constexpr uint32_t kMaxFrames = FractionalFrames<uint32_t>::Max().Floor();

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FRAMES_H_
