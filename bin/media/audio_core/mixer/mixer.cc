// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/mixer.h"

#include "garnet/bin/media/audio_core/mixer/linear_sampler.h"
#include "garnet/bin/media/audio_core/mixer/no_op.h"
#include "garnet/bin/media/audio_core/mixer/point_sampler.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

constexpr uint32_t Mixer::FRAC_ONE;
constexpr uint32_t Mixer::FRAC_MASK;

Mixer::~Mixer() {}

Mixer::Mixer(uint32_t pos_filter_width, uint32_t neg_filter_width)
    : pos_filter_width_(pos_filter_width),
      neg_filter_width_(neg_filter_width) {}

//
// Select an appropriate instance of a mixer based on the user-specified
// resampler type, else by the properties of source/destination formats.
//
// With 'resampler', users indicate which resampler they require. If not
// specified, or if Resampler::Default, the existing selection algorithm is
// used. Note that requiring a specific resampler may cause Mixer::Select() to
// fail (i.e. return nullptr), even in cases where 'Default' would succeed.
MixerPtr Mixer::Select(const fuchsia::media::AudioStreamType& src_format,
                       const fuchsia::media::AudioStreamType& dst_format,
                       Resampler resampler) {
  // If user specified a particular Resampler, directly select it.
  switch (resampler) {
    case Resampler::SampleAndHold:
      return mixer::PointSampler::Select(src_format, dst_format);
    case Resampler::LinearInterpolation:
      return mixer::LinearSampler::Select(src_format, dst_format);

      // Otherwise (if Default), continue onward.
    case Resampler::Default:
      break;
  }

  // If source sample rate is an integer multiple of destination sample rate,
  // just use the point sampler.  Otherwise, use the linear re-sampler.
  TimelineRate src_to_dst(src_format.frames_per_second,
                          dst_format.frames_per_second);
  if (src_to_dst.reference_delta() == 1) {
    return mixer::PointSampler::Select(src_format, dst_format);
  } else {
    return mixer::LinearSampler::Select(src_format, dst_format);
  }
}

}  // namespace audio
}  // namespace media
