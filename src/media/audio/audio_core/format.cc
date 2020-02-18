// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/format.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/mixer/frames.h"

namespace media::audio {

fit::result<Format> Format::Create(fuchsia::media::AudioStreamType stream_type) {
  // Sanity check the details of the mode request.
  if ((stream_type.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
      (stream_type.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
    FX_LOGS(ERROR) << "Bad channel count, " << stream_type.channels << " is not in the range ["
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT << ", "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT << "]";
    return fit::error();
  }

  if ((stream_type.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
      (stream_type.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
    FX_LOGS(ERROR) << "Bad frame rate, " << stream_type.frames_per_second
                   << " is not in the range [" << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << ", "
                   << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND << "]";
    return fit::error();
  }

  // Precompute some useful timing/format stuff.
  //
  // Start with the ratio between frames and nanoseconds.
  auto frames_per_ns = TimelineRate(stream_type.frames_per_second, ZX_SEC(1));

  // Figure out the rate we need to scale by in order to produce our fixed point timestamps.
  auto frame_to_media_ratio = TimelineRate(FractionalFrames<int32_t>(1).raw_value(), 1);

  uint32_t bytes_per_frame = 0;
  switch (stream_type.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      bytes_per_frame = 1;
      break;

    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      bytes_per_frame = 2;
      break;

    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
    case fuchsia::media::AudioSampleFormat::FLOAT:
      bytes_per_frame = 4;
      break;

    default:
      FX_LOGS(ERROR) << "Bad sample format " << fidl::ToUnderlying(stream_type.sample_format);
      return fit::error();
  }

  bytes_per_frame *= stream_type.channels;

  return fit::ok(Format(stream_type, frames_per_ns, frame_to_media_ratio, bytes_per_frame));
}

Format::Format(fuchsia::media::AudioStreamType stream_type, TimelineRate frames_per_ns,
               TimelineRate frame_to_media_ratio, uint32_t bytes_per_frame)
    : stream_type_(stream_type),
      frames_per_ns_(frames_per_ns),
      frame_to_media_ratio_(frame_to_media_ratio),
      bytes_per_frame_(bytes_per_frame) {}

bool Format::operator==(const Format& other) const {
  // All the other class members are derived from our stream_type, so we don't need to include them
  // here.
  return fidl::Equals(stream_type_, other.stream_type_);
}

}  // namespace media::audio
