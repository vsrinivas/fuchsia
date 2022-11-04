// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_CONSTANTS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_CONSTANTS_H_

#include <stdint.h>

#include <limits>

#include "src/media/audio/lib/format/constants.h"

namespace media::audio {

// Compile time constant guaranteed to never be used as a valid generation ID
// (by the various things which use generation IDs to track state changes).
constexpr uint32_t kInvalidGenerationId = 0;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_CONSTANTS_H_
