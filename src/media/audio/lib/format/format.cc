// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format/format.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format/traits.h"

namespace media::audio {

namespace {

bool Validate(fuchsia::media::AudioStreamType stream_type) {
  // Sanity check the details of the mode request.
  if ((stream_type.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (stream_type.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FX_LOGS(ERROR) << "Bad channel count, " << stream_type.channels << " is not in the range ["
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT << "]";
    return false;
  }

  if ((stream_type.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (stream_type.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FX_LOGS(ERROR) << "Bad frame rate, " << stream_type.frames_per_second
                   << " is not in the range [" << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
                   << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND << "]";
    return false;
  }
  switch (stream_type.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
    case fuchsia::media::AudioSampleFormat::FLOAT:
      break;
    default:
      FX_LOGS(ERROR) << "Bad sample format " << fidl::ToUnderlying(stream_type.sample_format);
      return false;
  }

  return true;
}

}  // namespace

fpromise::result<Format> Format::Create(fuchsia::media::AudioStreamType stream_type) {
  if (!Validate(stream_type)) {
    return fpromise::error();
  }
  return fpromise::ok(Format(stream_type));
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
fpromise::result<TypedFormat<SampleFormat>> Format::Create(int32_t channels,
                                                           int32_t frames_per_second) {
  fuchsia::media::AudioStreamType stream_type = {
      .sample_format = SampleFormat,
      .channels = static_cast<uint32_t>(channels),
      .frames_per_second = static_cast<uint32_t>(frames_per_second),
  };
  if (!Validate(stream_type)) {
    return fpromise::error();
  }
  return fpromise::ok(TypedFormat<SampleFormat>(stream_type));
}

Format::Format(fuchsia::media::AudioStreamType stream_type) : stream_type_(stream_type) {
  // Precompute some useful timing/format stuff.
  //
  // Start with the ratio between frames and nanoseconds.
  frames_per_ns_ = TimelineRate(stream_type.frames_per_second, ZX_SEC(1));

  // Figure out the rate we need to scale by in order to produce our fixed point timestamps.
  frame_to_media_ratio_ = TimelineRate(Fixed(1).raw_value(), 1);

  switch (stream_type.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      bytes_per_frame_ = 1;
      valid_bits_per_channel_ = 8;
      break;

    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      bytes_per_frame_ = 2;
      valid_bits_per_channel_ = 16;
      break;

    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      bytes_per_frame_ = 4;
      valid_bits_per_channel_ = 24;
      break;

    case fuchsia::media::AudioSampleFormat::FLOAT:
      bytes_per_frame_ = 4;
      valid_bits_per_channel_ = 32;
      break;
  }

  bytes_per_frame_ *= stream_type.channels;
}

bool Format::operator==(const Format& other) const {
  // All the other class members are derived from our stream_type, so we don't need to include them
  // here.
  return fidl::Equals(stream_type_, other.stream_type_);
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(T)                                                          \
  template fpromise::result<TypedFormat<T>> Format::Create<T>(int32_t channels, \
                                                              int32_t frames_per_seconds);

INSTANTIATE_FOR_ALL_FORMATS(INSTANTIATE)

}  // namespace media::audio
