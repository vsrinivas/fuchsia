// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/types.h"

#include "lib/fxl/logging.h"

namespace media {

uint32_t BytesPerSample(fuchsia::media::AudioSampleFormat sample_format) {
  switch (sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return sizeof(uint8_t);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return sizeof(int16_t);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return sizeof(int32_t);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return sizeof(float);
  }
}

fuchsia::media::AudioStreamType CreateAudioStreamType(
    fuchsia::media::AudioSampleFormat sample_format, uint32_t channel_count,
    uint32_t frames_per_second) {
  FXL_DCHECK(channel_count != 0);

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = sample_format;
  audio_stream_type.channels = channel_count;
  audio_stream_type.frames_per_second = frames_per_second;

  return audio_stream_type;
}

}  // namespace media
