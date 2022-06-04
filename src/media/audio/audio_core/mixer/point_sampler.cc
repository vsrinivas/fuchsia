// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <memory>
#include <utility>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/audio_core/mixer/position_manager.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/processing/point_sampler.h"
#include "src/media/audio/lib/processing/sampler.h"

namespace media::audio::mixer {

namespace {

using ::media_audio::Fixed;
using ::media_audio::Sampler;

// Although selected by enum Resampler::SampleAndHold, the `PointSampler` implementation is actually
// "nearest neighbor", and specifically "forward nearest neighbor" because for a sampling position
// exactly midway between two source frames, we choose the newer one. Thus pos_width and neg_width
// are both approximately kHalfFrame, but pos_width > neg_width.
//
// If this implementation were actually "sample and hold", pos_width would be 0 and neg_width
// would be kOneFrame.raw_value() - 1.
//
// Why isn't this a truly "zero-phase" implementation? Here's why:
// For zero-phase, both filter widths are kHalfFrame.raw_value(), and to sample exactly midway
// between two source frames we return their AVERAGE. This makes a nearest-neighbor resampler
// truly zero-phase at ALL rate-conversion ratios, even though at that one particular position
// (exactly halfway bewtween frames) it behaves differently than it does for other positions:
// at that position it actually behaves like a linear-interpolation resampler by returning a
// "blur" of two neighbors. As with a linear resampler, this decreases output response at higher
// frequencies, but only to the extent that this resampler encounters that exact position. For
// arbitrary rate-conversion ratios, this effect is negligible, thus zero-phase point samplers are
// generally preferred to other implementation types such as strict "sample and hold".
// HOWEVER, in our system we use `PointSampler` only for UNITY rate-conversion. Thus if it needs to
// output a frame from a position exactly halfway between two source frames, it will likely need
// to do so for EVERY frame in that stream, leading to that stream sounding muffled or indistinct
// (from reduced high frequency content). This might be more frequently triggered by certain
// circumstances, but in (arguably) the worst-case scenario this would occur perhaps once out of
// every 8192 times (our fractional position precision).
//
// For this reason, we arbitrarily choose the forward source frame rather than averaging.
// Assuming that we continue limiting `PointSampler` to only UNITY rate-conversion scenarios, one
// could reasonably argue that "sample and hold" would actually be optimal: phase is moot for 1:1
// sampling so we receive no benefit from the additional half-frame of latency.
//

//
// As an optimization, we work with raw fixed-point values internally, but we pass Fixed types
// through our public interfaces (to MixStage etc.) for source position/filter width/step size.
constexpr int64_t kFracPositiveFilterWidth = media_audio::kHalfFrame.raw_value();
constexpr int64_t kFracNegativeFilterWidth = kFracPositiveFilterWidth - 1;

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
    MakePublicCtor(Fixed pos_filter_width, Fixed neg_filter_width, Gain::Limits gain_limits,
                   std::shared_ptr<Sampler> point_sampler)
        : PointSampler(pos_filter_width, neg_filter_width, gain_limits, std::move(point_sampler)) {}
  };
  return std::make_unique<MakePublicCtor>(Fixed::FromRaw(kFracPositiveFilterWidth),
                                          Fixed::FromRaw(kFracNegativeFilterWidth), gain_limits,
                                          std::move(point_sampler));
}

void PointSampler::Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                       const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
                       bool accumulate) {
  TRACE_DURATION("audio", "PointSampler::Mix");

  auto info = &bookkeeping();
  // CheckPositions expects a frac_pos_filter_length value that _includes_ [0], thus the '+1'
  // TODO(fxbug.dev/72561): Convert Mixer class and the rest of audio_core to define filter width as
  // including the center position in its count (as PositionManager and Filter::Length do). Then the
  // distinction between filter length and filter width would go away, this kFracPositiveFilterWidth
  // constant would be changed, and the below "+ 1" would be removed.
  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, source_frames,
                                  source_offset_ptr->raw_value(), kFracPositiveFilterWidth + 1,
                                  info);

  Sampler::Source source{source_void_ptr, source_offset_ptr, source_frames};
  Sampler::Dest dest{dest_ptr, dest_offset_ptr, dest_frames};
  if (info->gain.IsSilent()) {
    // If the gain is silent, the mixer simply skips over the appropriate range in the destination
    // buffer, leaving whatever data is already there. We do not take further effort to clear the
    // buffer if `accumulate` is false. In fact, we IGNORE `accumulate` if silent. The caller is
    // responsible for clearing the destination buffer before Mix is initially called.
    point_sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kSilent},
                            true);
  } else if (info->gain.IsUnity()) {
    point_sampler_->Process(source, dest, Sampler::Gain{.type = media_audio::GainType::kUnity},
                            accumulate);
  } else if (info->gain.IsRamping()) {
    point_sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kRamping, .scale_ramp = info->scale_arr.get()},
        accumulate);
  } else {
    point_sampler_->Process(
        source, dest,
        Sampler::Gain{.type = media_audio::GainType::kNonUnity, .scale = info->gain.GetGainScale()},
        accumulate);
  }
}

}  // namespace media::audio::mixer
