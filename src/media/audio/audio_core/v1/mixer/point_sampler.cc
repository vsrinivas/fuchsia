// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/point_sampler.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <memory>
#include <utility>

#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/processing/point_sampler.h"
#include "src/media/audio/lib/processing/sampler.h"

namespace media::audio::mixer {

namespace {

using ::media_audio::Fixed;
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

std::unique_ptr<Mixer> PointSampler::Select(const fuchsia::media::AudioStreamType& source_format,
                                            const fuchsia::media::AudioStreamType& dest_format,
                                            Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "PointSampler::Select");

  auto point_sampler =
      media_audio::PointSampler::Create(ToNewFormat(source_format), ToNewFormat(dest_format));
  if (!point_sampler) {
    return nullptr;
  }

  struct MakePublicCtor : PointSampler {
    MakePublicCtor(Gain::Limits gain_limits, std::shared_ptr<Sampler> point_sampler)
        : PointSampler(gain_limits, std::move(point_sampler)) {}
  };
  return std::make_unique<MakePublicCtor>(gain_limits, std::move(point_sampler));
}

void PointSampler::Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                       const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                       bool accumulate) {
  TRACE_DURATION("audio", "PointSampler::Mix");

  Sampler::Source source{source_void_ptr, source_offset_ptr, source_frames};
  Sampler::Dest dest{dest_ptr, dest_offset_ptr, dest_frames};
  if (gain.IsSilent()) {
    // If the gain is silent, the mixer simply skips over the appropriate range in the destination
    // buffer, leaving whatever data is already there. We do not take further effort to clear the
    // buffer if `accumulate` is false. In fact, we IGNORE `accumulate` if silent. The caller is
    // responsible for clearing the destination buffer before Mix is initially called.
    sampler().Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kSilent}, true);
  } else if (gain.IsUnity()) {
    sampler().Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kUnity},
                      accumulate);
  } else if (gain.IsRamping()) {
    sampler().Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kRamping, .scale_ramp = scale_arr.get()},
        accumulate);
  } else {
    sampler().Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kNonUnity, .scale = gain.GetGainScale()},
        accumulate);
  }
}

}  // namespace media::audio::mixer
