// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/sinc_sampler.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <memory>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/processing/position_manager.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/lib/processing/sinc_sampler.h"

namespace media::audio::mixer {

namespace {

using ::media_audio::PositionManager;
using ::media_audio::Sampler;

fuchsia_mediastreams::wire::AudioSampleFormat ToNewSampleFormat(
    fuchsia::media::AudioSampleFormat sample_format) {
  switch (sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return fuchsia_mediastreams::wire::AudioSampleFormat::kUnsigned8;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return fuchsia_mediastreams::wire::AudioSampleFormat::kSigned16;
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return fuchsia_mediastreams::wire::AudioSampleFormat::kSigned24In32;
    case fuchsia::media::AudioSampleFormat::FLOAT:
    default:
      return fuchsia_mediastreams::wire::AudioSampleFormat::kFloat;
  }
}

media_audio::Format ToNewFormat(const fuchsia::media::AudioStreamType& format) {
  return media_audio::Format::CreateOrDie(
      {ToNewSampleFormat(format.sample_format), format.channels, format.frames_per_second});
}

}  // namespace

std::unique_ptr<Mixer> SincSampler::Select(const fuchsia::media::AudioStreamType& source_format,
                                           const fuchsia::media::AudioStreamType& dest_format,
                                           Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "SincSampler::Select");

  auto sinc_sampler =
      media_audio::SincSampler::Create(ToNewFormat(source_format), ToNewFormat(dest_format));
  if (!sinc_sampler) {
    return nullptr;
  }

  struct MakePublicCtor : SincSampler {
    MakePublicCtor(Gain::Limits gain_limits, std::unique_ptr<media_audio::SincSampler> sinc_sampler)
        : SincSampler(gain_limits, std::move(sinc_sampler)) {}
  };
  return std::make_unique<MakePublicCtor>(
      gain_limits, std::unique_ptr<media_audio::SincSampler>(
                       static_cast<media_audio::SincSampler*>(sinc_sampler.release())));
}

void SincSampler::EagerlyPrepare() { sinc_sampler_->EagerlyPrepare(); }

void SincSampler::Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                      const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                      bool accumulate) {
  TRACE_DURATION("audio", "SincSampler::Mix");

  auto info = &bookkeeping();
  auto& position_manager = sinc_sampler_->position_manager();
  PositionManager::CheckPositions(
      dest_frames, dest_offset_ptr, source_frames, source_offset_ptr->raw_value(),
      sinc_sampler_->pos_filter_length().raw_value(), info->step_size().raw_value(),
      info->rate_modulo(), info->denominator(), info->source_pos_modulo());
  position_manager.SetRateValues(info->step_size().raw_value(), info->rate_modulo(),
                                 info->denominator(), info->source_pos_modulo());

  Sampler::Source source{source_void_ptr, source_offset_ptr, source_frames};
  Sampler::Dest dest{dest_ptr, dest_offset_ptr, dest_frames};
  if (info->gain.IsSilent()) {
    // If the gain is silent, the mixer simply skips over the appropriate range in the destination
    // buffer, leaving whatever data is already there. We do not take further effort to clear the
    // buffer if `accumulate` is false. In fact, we IGNORE `accumulate` if silent. The caller is
    // responsible for clearing the destination buffer before Mix is initially called.
    sinc_sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kSilent},
                           true);
  } else if (info->gain.IsUnity()) {
    sinc_sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kUnity},
                           accumulate);
  } else if (info->gain.IsRamping()) {
    sinc_sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kRamping, .scale_ramp = info->scale_arr.get()},
        accumulate);
  } else {
    sinc_sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kNonUnity, .scale = info->gain.GetGainScale()},
        accumulate);
  }

  if (info->rate_modulo() > 0) {
    info->set_source_pos_modulo(position_manager.source_pos_modulo());
  }
}

}  // namespace media::audio::mixer
