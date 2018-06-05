// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_AUDIO_TYPES_H_
#define LIB_MEDIA_AUDIO_TYPES_H_

#include <cstdint>

#include <fuchsia/media/cpp/fidl.h>

namespace media {

// Template struct for determining traits of sample types.
template <typename Sample>
struct SampleTypeTraits;

template <>
struct SampleTypeTraits<uint8_t> {
  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::UNSIGNED_8;
};

template <>
struct SampleTypeTraits<int16_t> {
  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::SIGNED_16;
};

template <>
struct SampleTypeTraits<int32_t> {
  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
};

template <>
struct SampleTypeTraits<float> {
  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::FLOAT;
};

// Returns the size in bytes of samples of the specified format.
uint32_t BytesPerSample(fuchsia::media::AudioSampleFormat format);

// Creates a |MediaType| for LPCM audio.
// TODO(dalesat): Need to add channel configuration.
fuchsia::media::MediaType CreateLpcmMediaType(
    fuchsia::media::AudioSampleFormat sample_format, uint32_t channel_count,
    uint32_t frames_per_second);

}  // namespace media

#endif  // LIB_MEDIA_AUDIO_TYPES_H_
