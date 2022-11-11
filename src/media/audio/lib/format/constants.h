// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_

#include <ffl/string.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media::audio {

// TODO(fxbug.dev/114920): Temporary alias to avoid extra noise in the existing audio_core codebase.
using Fixed = media_audio::Fixed;
inline constexpr int32_t kPtsFractionalBits = media_audio::kPtsFractionalBits;
inline constexpr Fixed kOneFrame = media_audio::kOneFrame;
inline constexpr Fixed kHalfFrame = media_audio::kHalfFrame;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_CONSTANTS_H_
