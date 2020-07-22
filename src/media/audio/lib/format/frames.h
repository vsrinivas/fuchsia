// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_FRAMES_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_FRAMES_H_

#include <ffl/fixed.h>

#include "src/media/audio/lib/format/constants.h"

namespace media::audio {

template <typename Integer>
using FractionalFrames = ffl::Fixed<Integer, kPtsFractionalBits>;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_FRAMES_H_
