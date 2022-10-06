// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/mixer.h"

#include <lib/trace/event.h>

#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/mixer/point_sampler.h"
#include "src/media/audio/audio_core/mixer/sinc_sampler.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

constexpr int64_t Mixer::kScaleArrLen;

Mixer::Mixer(Fixed pos_filter_width, Fixed neg_filter_width,
             std::shared_ptr<media_audio::Sampler> sampler, Gain::Limits gain_limits)
    : gain(gain_limits),
      pos_filter_width_(pos_filter_width),
      neg_filter_width_(neg_filter_width),
      sampler_(std::move(sampler)) {}

//
// Select an appropriate instance of a mixer based on the user-specified
// resampler type, else by the properties of source/destination formats.
//
// With 'resampler', users indicate which resampler they require. If not
// specified, or if Resampler::Default, the existing selection algorithm is
// used. Note that requiring a specific resampler may cause Mixer::Select() to
// fail (i.e. return nullptr), even in cases where 'Default' would succeed.
std::unique_ptr<Mixer> Mixer::Select(const fuchsia::media::AudioStreamType& source_format,
                                     const fuchsia::media::AudioStreamType& dest_format,
                                     Resampler resampler, Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "Mixer::Select");

  if (source_format.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND ||
      dest_format.frames_per_second > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    FX_LOGS(WARNING) << "Mixer frame rates (" << source_format.frames_per_second << ":"
                     << dest_format.frames_per_second << ") cannot exceed "
                     << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND;
    return nullptr;
  }

  if (source_format.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND ||
      dest_format.frames_per_second < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    FX_LOGS(WARNING) << "Mixer frame rates (" << source_format.frames_per_second << ":"
                     << dest_format.frames_per_second << ") must be at least "
                     << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND;
    return nullptr;
  }

  if (source_format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT ||
      dest_format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    FX_LOGS(WARNING) << "Mixer channel counts (" << source_format.channels << ":"
                     << dest_format.channels << ") cannot exceed "
                     << fuchsia::media::MAX_PCM_CHANNEL_COUNT;
    return nullptr;
  }

  if (source_format.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT ||
      dest_format.channels < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    FX_LOGS(WARNING) << "Mixer frame rates (" << source_format.channels << ":"
                     << dest_format.channels << ") must be at least "
                     << fuchsia::media::MIN_PCM_CHANNEL_COUNT;
    return nullptr;
  }

  if (source_format.sample_format != fuchsia::media::AudioSampleFormat::UNSIGNED_8 &&
      source_format.sample_format != fuchsia::media::AudioSampleFormat::SIGNED_16 &&
      source_format.sample_format != fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32 &&
      source_format.sample_format != fuchsia::media::AudioSampleFormat::FLOAT) {
    FX_LOGS(WARNING) << "Unsupported mixer sample format "
                     << static_cast<int64_t>(source_format.sample_format);
    return nullptr;
  }

  // If user specified a particular Resampler, directly select it.
  switch (resampler) {
    case Resampler::SampleAndHold:
      return mixer::PointSampler::Select(source_format, dest_format, gain_limits);
    case Resampler::WindowedSinc:
      return mixer::SincSampler::Select(source_format, dest_format, gain_limits);

      // Otherwise (if Default), continue onward.
    case Resampler::Default:
      break;
  }

  // Use SampleAndHold if no rate conversion (unity 1:1). Otherwise, use WindowedSinc (with
  // integrated low-pass filter).
  TimelineRate source_to_dest(dest_format.frames_per_second, source_format.frames_per_second);
  if (source_to_dest.subject_delta() == 1 && source_to_dest.reference_delta() == 1) {
    return mixer::PointSampler::Select(source_format, dest_format, gain_limits);
  } else {
    return mixer::SincSampler::Select(source_format, dest_format, gain_limits);
  }
}

}  // namespace media::audio
