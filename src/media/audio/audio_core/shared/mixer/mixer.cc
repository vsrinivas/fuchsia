// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/mixer.h"

#include <fidl/fuchsia.audio/cpp/common_types.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/trace/event.h>

#include <memory>

#include "src/media/audio/audio_core/shared/mixer/no_op_sampler.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

namespace {

using ::media_audio::Sampler;

fuchsia_audio::SampleType ToNewSampleType(fuchsia::media::AudioSampleFormat sample_format) {
  switch (sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return fuchsia_audio::SampleType::kUint8;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return fuchsia_audio::SampleType::kInt16;
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return fuchsia_audio::SampleType::kInt32;
    case fuchsia::media::AudioSampleFormat::FLOAT:
    default:
      return fuchsia_audio::SampleType::kFloat32;
  }
}

media_audio::Format ToNewFormat(const fuchsia::media::AudioStreamType& format) {
  return media_audio::Format::CreateOrDie({
      .sample_type = ToNewSampleType(format.sample_format),
      .channels = format.channels,
      .frames_per_second = format.frames_per_second,
  });
}

}  // namespace

Mixer::Mixer(std::shared_ptr<Sampler> sampler, Gain::Limits gain_limits)
    : gain(gain_limits),
      pos_filter_width_(sampler->pos_filter_length() - media_audio::Fixed::FromRaw(1)),
      neg_filter_width_(sampler->neg_filter_length() - media_audio::Fixed::FromRaw(1)),
      sampler_(std::move(sampler)) {
  FX_CHECK(sampler_);
}

std::unique_ptr<Mixer> Mixer::NoOp() {
  return std::make_unique<Mixer>(std::make_shared<NoOpSampler>(), Gain::Limits{});
}

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

  const auto sampler_type = (resampler == Resampler::WindowedSinc) ? Sampler::Type::kSincSampler
                                                                   : Sampler::Type::kDefault;
  return std::make_unique<Mixer>(
      Sampler::Create(ToNewFormat(source_format), ToNewFormat(dest_format), sampler_type),
      gain_limits);
}

void Mixer::Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                bool accumulate) {
  TRACE_DURATION("audio", "Mixer::Mix");

  Sampler::Source source{source_void_ptr, source_offset_ptr, source_frames};
  Sampler::Dest dest{dest_ptr, dest_offset_ptr, dest_frames};
  if (gain.IsSilent()) {
    // If the gain is silent, the mixer simply skips over the appropriate range in the destination
    // buffer, leaving whatever data is already there. We do not take further effort to clear the
    // buffer if `accumulate` is false. In fact, we IGNORE `accumulate` if silent. The caller is
    // responsible for clearing the destination buffer before Mix is initially called.
    sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kSilent}, true);
  } else if (gain.IsUnity()) {
    sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kUnity},
                      accumulate);
  } else if (gain.IsRamping()) {
    sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kRamping, .scale_ramp = scale_arr.get()},
        accumulate);
  } else {
    sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kNonUnity, .scale = gain.GetGainScale()},
        accumulate);
  }
}

}  // namespace media::audio
