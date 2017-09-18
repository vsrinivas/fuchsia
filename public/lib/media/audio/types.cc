// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/types.h"

namespace media {

uint32_t BytesPerSample(AudioSampleFormat sample_format) {
  switch (sample_format) {
    case AudioSampleFormat::NONE:
    case AudioSampleFormat::ANY:
      return 0;
    case AudioSampleFormat::UNSIGNED_8:
      return sizeof(uint8_t);
    case AudioSampleFormat::SIGNED_16:
      return sizeof(int16_t);
    case AudioSampleFormat::SIGNED_24_IN_32:
      return sizeof(int32_t);
    case AudioSampleFormat::FLOAT:
      return sizeof(float);
  }
}

MediaTypePtr CreateLpcmMediaType(AudioSampleFormat sample_format,
                                 uint32_t channel_count,
                                 uint32_t frames_per_second) {
  AudioMediaTypeDetailsPtr audio_details = AudioMediaTypeDetails::New();

  audio_details->sample_format = sample_format;
  audio_details->channels = channel_count;
  audio_details->frames_per_second = frames_per_second;

  MediaTypeDetailsPtr media_details = MediaTypeDetails::New();
  media_details->set_audio(std::move(audio_details));

  MediaTypePtr media_type = MediaType::New();
  media_type->medium = MediaTypeMedium::AUDIO;
  media_type->details = std::move(media_details);
  media_type->encoding = MediaType::kAudioEncodingLpcm;

  return media_type;
}

}  // namespace media
