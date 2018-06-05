// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/types.h"

namespace media {

uint32_t BytesPerSample(fuchsia::media::AudioSampleFormat sample_format) {
  switch (sample_format) {
    case fuchsia::media::AudioSampleFormat::NONE:
    case fuchsia::media::AudioSampleFormat::ANY:
      return 0;
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

fuchsia::media::MediaType CreateLpcmMediaType(
    fuchsia::media::AudioSampleFormat sample_format, uint32_t channel_count,
    uint32_t frames_per_second) {
  fuchsia::media::AudioMediaTypeDetails audio_details;
  audio_details.sample_format = sample_format;
  audio_details.channels = channel_count;
  audio_details.frames_per_second = frames_per_second;

  fuchsia::media::MediaTypeDetails media_details;
  media_details.set_audio(std::move(audio_details));

  fuchsia::media::MediaType media_type;
  media_type.medium = fuchsia::media::MediaTypeMedium::AUDIO;
  media_type.details = std::move(media_details);
  media_type.encoding = fuchsia::media::kAudioEncodingLpcm;

  return media_type;
}

}  // namespace media
