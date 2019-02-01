// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_AUDIO_TYPES_H_
#define LIB_MEDIA_AUDIO_TYPES_H_

#include <cstdint>

#include <fuchsia/media/cpp/fidl.h>

namespace media {

// Returns the size in bytes of samples of the specified format.
uint32_t BytesPerSample(fuchsia::media::AudioSampleFormat format);

// Creates an |AudioStreamType| for LPCM audio. |channel_count| may not be zero.
fuchsia::media::AudioStreamType CreateAudioStreamType(
    fuchsia::media::AudioSampleFormat sample_format, uint32_t channel_count,
    uint32_t frames_per_second);

}  // namespace media

#endif  // LIB_MEDIA_AUDIO_TYPES_H_
