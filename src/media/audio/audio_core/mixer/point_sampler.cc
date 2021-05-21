// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <limits>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"
#include "src/media/audio/audio_core/mixer/position_manager.h"

namespace media::audio::mixer {

// Point Sample Mixer implementation.
template <int32_t DestChanCount, typename SourceSampleType, int32_t SourceChanCount>
class PointSamplerImpl : public PointSampler {
 public:
  explicit PointSamplerImpl(Gain::Limits gain_limits)
      : PointSampler(Fixed::FromRaw(kFracPositiveFilterWidth),
                     Fixed::FromRaw(kFracNegativeFilterWidth), gain_limits) {}

  bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
           const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
           bool accumulate) override;

 private:
  // Although selected by enum Resampler::SampleAndHold, the PointSampler implementation is actually
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
  // HOWEVER, in our system we use PointSampler only for UNITY rate-conversion. Thus if it needs to
  // output a frame from a position exactly halfway between two source frames, it will likely need
  // to do so for EVERY frame in that stream, leading to that stream sounding muffled or indistinct
  // (from reduced high frequency content). This might be more frequently triggered by certain
  // circumstances, but in (arguably) the worst-case scenario this would occur perhaps once out of
  // every 8192 times (our fractional position precision).
  //
  // For this reason, we arbitrarily choose the forward source frame rather than averaging.
  // Assuming that we continue limiting PointSampler to only UNITY rate-conversion scenarios, one
  // could reasonably argue that "sample and hold" would actually be optimal: phase is moot for 1:1
  // sampling so we receive no benefit from the additional half-frame of latency.
  //

  //
  // As an optimization, we work with raw fixed-point values internally, but we pass Fixed types
  // through our public interfaces (to MixStage etc.) for source position/filter width/step size.
  static constexpr int64_t kFracPositiveFilterWidth = kHalfFrame.raw_value();
  static constexpr int64_t kFracNegativeFilterWidth = kFracPositiveFilterWidth - 1;

  static int64_t Ceiling(int64_t frac_position) {
    return ((frac_position - 1) >> Fixed::Format::FractionalBits) + 1;
  }
  static int64_t Floor(int64_t frac_position) {
    return frac_position >> Fixed::Format::FractionalBits;
  }

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                         const void* source_void_ptr, int64_t source_frames,
                         Fixed* source_offset_ptr, Bookkeeping* info);
};

// TODO(fxbug.dev/13361): refactor to minimize code duplication, or even better eliminate NxN
// implementations altogether, replaced by flexible rechannelization (fxbug.dev/13679).
template <typename SourceSampleType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(int32_t chan_count, Gain::Limits gain_limits)
      : PointSampler(Fixed::FromRaw(kFracPositiveFilterWidth),
                     Fixed::FromRaw(kFracNegativeFilterWidth), gain_limits),
        chan_count_(chan_count) {}

  bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
           const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
           bool accumulate) override;

 private:
  static constexpr int64_t kFracPositiveFilterWidth = kHalfFrame.raw_value();
  static constexpr int64_t kFracNegativeFilterWidth = kFracPositiveFilterWidth - 1;

  static int64_t Ceiling(int64_t frac_position) {
    return ((frac_position - 1) >> Fixed::Format::FractionalBits) + 1;
  }
  static int64_t Floor(int64_t frac_position) {
    return frac_position >> Fixed::Format::FractionalBits;
  }

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                         const void* source_void_ptr, int64_t source_frames,
                         Fixed* source_offset_ptr, Bookkeeping* info, int32_t chan_count);
  int32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <int32_t DestChanCount, typename SourceSampleType, int32_t SourceChanCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool PointSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr, const void* source_void_ptr,
    int64_t source_frames, Fixed* source_offset_ptr, Bookkeeping* info) {
  TRACE_DURATION("audio", "PointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using DM = DestMixer<ScaleType, DoAccumulate>;
  auto dest_offset = *dest_offset_ptr;

  using SR = SourceReader<SourceSampleType, SourceChanCount, DestChanCount>;
  auto frac_source_offset = source_offset_ptr->raw_value();
  const auto* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);

  // frac_source_end is the first subframe for which this Mix call cannot produce output. Producing
  // output centered on this source position (or beyond) requires data that we don't have yet.
  const int64_t frac_source_end =
      (source_frames << Fixed::Format::FractionalBits) - kFracPositiveFilterWidth;

  // Source_offset can be as large as source_frames. All samplers should produce no output and
  // return true; those with significant history can also "prime" (cache) previous data as needed.
  if (frac_source_offset >= frac_source_end) {
    return true;
  }

  auto frames_to_mix =
      std::min<int64_t>(Ceiling(frac_source_end - frac_source_offset), dest_frames - dest_offset);
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    int64_t source_sample_idx =
        Floor(frac_source_offset + kFracPositiveFilterWidth) * SourceChanCount;
    float* out = dest_ptr + (dest_offset * DestChanCount);

    for (auto frame_num = 0; frame_num < frames_to_mix; ++frame_num) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[frame_num];
      }

      for (int32_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
        float sample = SR::Read(source_ptr + source_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      source_sample_idx += SourceChanCount;
      out += DestChanCount;
    }
  }
  // Otherwise we're muted, but we'll advance frac_source_offset and dest_offset as if we produced
  // data.

  // Either way, update all our returned in-out parameters
  frac_source_offset += (frames_to_mix << Fixed::Format::FractionalBits);
  *source_offset_ptr = Fixed::FromRaw(frac_source_offset);
  *dest_offset_ptr = dest_offset + static_cast<uint32_t>(frames_to_mix);

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_source_offset >= frac_source_end);
}

// Regarding ScalerType::MUTED - in that specialization, the mixer simply skips over the appropriate
// range in the destination buffer, leaving whatever data is already there. We do not take further
// effort to clear the buffer if 'accumulate' is false. In fact, we IGNORE 'accumulate' if MUTED.
// The caller is responsible for clearing the destination buffer before Mix is initially called.
// DoAccumulate is still valuable in the non-mute case, as it saves a read+FADD per sample.
//
template <int32_t DestChanCount, typename SourceSampleType, int32_t SourceChanCount>
bool PointSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr, const void* source_void_ptr,
    int64_t source_frames, Fixed* source_offset_ptr, bool accumulate) {
  TRACE_DURATION("audio", "PointSamplerImpl::Mix");

  auto info = &bookkeeping();
  // CheckPositions expects a frac_pos_filter_length value that _includes_ [0], thus the '+1'
  // TODO(fxbug.dev/72561): Convert Mixer class and the rest of audio_core to define filter width as
  // including the center position in its count (as PositionManager and Filter::Length do). Then the
  // distinction between filter length and filter width would go away, this kFracPositiveFilterWidth
  // constant would be changed, and the below "+ 1" would be removed.
  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, source_frames,
                                  source_offset_ptr->raw_value(), kFracPositiveFilterWidth + 1,
                                  info);

  if (info->gain.IsUnity()) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, source_frames,
                                                        source_offset_ptr, info)
                      : Mix<ScalerType::EQ_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                         source_void_ptr, source_frames,
                                                         source_offset_ptr, info);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest_ptr, dest_frames, dest_offset_ptr, source_void_ptr,
                                        source_frames, source_offset_ptr, info);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, source_frames,
                                                       source_offset_ptr, info)
                      : Mix<ScalerType::RAMPING, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, source_frames,
                                                        source_offset_ptr, info);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                      source_void_ptr, source_frames,
                                                      source_offset_ptr, info)
                    : Mix<ScalerType::NE_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, source_frames,
                                                       source_offset_ptr, info);
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <typename SourceSampleType>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool NxNPointSamplerImpl<SourceSampleType>::Mix(
    float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr, const void* source_void_ptr,
    int64_t source_frames, Fixed* source_offset_ptr, Bookkeeping* info, int32_t chan_count) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SourceReader<SourceSampleType, 1, 1>;
  using DM = DestMixer<ScaleType, DoAccumulate>;
  auto dest_offset = *dest_offset_ptr;

  auto frac_source_offset = source_offset_ptr->raw_value();
  const auto* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);

  // the first subframe for which this Mix call cannot produce output. Producing output centered on
  // this source position (or beyond) requires data that we don't have yet.
  const int64_t frac_source_end =
      (source_frames << Fixed::Format::FractionalBits) - kFracPositiveFilterWidth;

  // Source_offset can be as large as source_frames. All samplers should produce no output and
  // return true; those with significant history can also "prime" (cache) previous data as needed.
  if (frac_source_offset >= frac_source_end) {
    return true;
  }

  auto frames_to_mix =
      std::min<int64_t>(Ceiling(frac_source_end - frac_source_offset), dest_frames - dest_offset);
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    int64_t source_sample_idx = Floor(frac_source_offset + kFracPositiveFilterWidth) * chan_count;
    float* out = dest_ptr + (dest_offset * chan_count);

    for (auto frame_num = 0; frame_num < frames_to_mix; ++frame_num) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[frame_num];
      }

      for (int32_t dest_chan = 0; dest_chan < chan_count; ++dest_chan) {
        float sample = SR::Read(source_ptr + source_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      source_sample_idx += chan_count;
      out += chan_count;
    }
  }
  // Otherwise we're muted, but advance frac_source_offset and dest_offset as if we produced data.

  // Either way, update all our returned in-out parameters
  frac_source_offset += (frames_to_mix << Fixed::Format::FractionalBits);
  *source_offset_ptr = Fixed::FromRaw(frac_source_offset);
  *dest_offset_ptr = dest_offset + static_cast<uint32_t>(frames_to_mix);

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_source_offset >= frac_source_end);
}

template <typename SourceSampleType>
bool NxNPointSamplerImpl<SourceSampleType>::Mix(float* dest_ptr, int64_t dest_frames,
                                                int64_t* dest_offset_ptr,
                                                const void* source_void_ptr, int64_t source_frames,
                                                Fixed* source_offset_ptr, bool accumulate) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::Mix");

  auto info = &bookkeeping();
  // CheckPositions expects a frac_pos_filter_length value that _includes_ [0], thus the '+1'
  // TODO(fxbug.dev/72561): Convert Mixer class and the rest of audio_core to define filter width as
  // including the center position in its count (as PositionManager and Filter::Length do). Then the
  // distinction between filter length and filter width would go away, this kFracPositiveFilterWidth
  // constant would be changed, and the below "+ 1" would be removed.
  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, source_frames,
                                  source_offset_ptr->raw_value(), kFracPositiveFilterWidth + 1,
                                  info);

  if (info->gain.IsUnity()) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, source_frames,
                                                        source_offset_ptr, info, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                         source_void_ptr, source_frames,
                                                         source_offset_ptr, info, chan_count_);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest_ptr, dest_frames, dest_offset_ptr, source_void_ptr,
                                        source_frames, source_offset_ptr, info, chan_count_);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, source_frames,
                                                       source_offset_ptr, info, chan_count_)
                      : Mix<ScalerType::RAMPING, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, source_frames,
                                                        source_offset_ptr, info, chan_count_);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                      source_void_ptr, source_frames,
                                                      source_offset_ptr, info, chan_count_)
                    : Mix<ScalerType::NE_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, source_frames,
                                                       source_offset_ptr, info, chan_count_);
}

// Templates used to expand the combinations of possible PointSampler configurations.
template <int32_t DestChanCount, typename SourceSampleType, int32_t SourceChanCount>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format,
                                               Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType,sChan)");
  return std::make_unique<PointSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>>(
      gain_limits);
}

template <int32_t DestChanCount, typename SourceSampleType>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format,
                                               Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType)");

  switch (source_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return SelectPSM<DestChanCount, SourceSampleType, 1>(source_format, dest_format,
                                                             gain_limits);
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return SelectPSM<DestChanCount, SourceSampleType, 2>(source_format, dest_format,
                                                             gain_limits);
      }
      break;
    case 3:
      if constexpr (DestChanCount <= 2) {
        return SelectPSM<DestChanCount, SourceSampleType, 3>(source_format, dest_format,
                                                             gain_limits);
      }
      break;
    case 4:
      if constexpr (DestChanCount <= 2) {
        return SelectPSM<DestChanCount, SourceSampleType, 4>(source_format, dest_format,
                                                             gain_limits);
      }
      break;
    default:
      break;
  }
  return nullptr;
}

template <int32_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format,
                                               Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "SelectPSM(dChan)");

  switch (source_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectPSM<DestChanCount, uint8_t>(source_format, dest_format, gain_limits);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectPSM<DestChanCount, int16_t>(source_format, dest_format, gain_limits);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectPSM<DestChanCount, int32_t>(source_format, dest_format, gain_limits);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectPSM<DestChanCount, float>(source_format, dest_format, gain_limits);
    default:
      return nullptr;
  }
}

static inline std::unique_ptr<Mixer> SelectNxNPSM(
    const fuchsia::media::AudioStreamType& source_format, Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "SelectNxNPSM");
  switch (source_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return std::make_unique<NxNPointSamplerImpl<uint8_t>>(source_format.channels, gain_limits);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return std::make_unique<NxNPointSamplerImpl<int16_t>>(source_format.channels, gain_limits);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return std::make_unique<NxNPointSamplerImpl<int32_t>>(source_format.channels, gain_limits);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return std::make_unique<NxNPointSamplerImpl<float>>(source_format.channels, gain_limits);
    default:
      return nullptr;
  }
}

std::unique_ptr<Mixer> PointSampler::Select(const fuchsia::media::AudioStreamType& source_format,
                                            const fuchsia::media::AudioStreamType& dest_format,
                                            Gain::Limits gain_limits) {
  TRACE_DURATION("audio", "PointSampler::Select");

  if (source_format.frames_per_second != dest_format.frames_per_second) {
    FX_LOGS(WARNING) << "PointSampler source frame rate " << source_format.frames_per_second
                     << " must equal dest frame rate " << dest_format.frames_per_second;
    return nullptr;
  }

  // If num_channels for source and dest are equal and > 2, directly map these one-to-one.
  // TODO(fxbug.dev/13361): eliminate NxN mixers; replace w/ flexible rechannelization (see below).
  if (source_format.channels == dest_format.channels && source_format.channels > 2) {
    return SelectNxNPSM(source_format, gain_limits);
  }

  if (source_format.channels < 1 || source_format.channels > 4) {
    FX_LOGS(WARNING) << "PointSampler does not support this channelization: "
                     << source_format.channels << " -> " << dest_format.channels;
    return nullptr;
  }

  switch (dest_format.channels) {
    case 1:
      return SelectPSM<1>(source_format, dest_format, gain_limits);
    case 2:
      return SelectPSM<2>(source_format, dest_format, gain_limits);
    case 3:
      return SelectPSM<3>(source_format, dest_format, gain_limits);
    case 4:
      // For now, to mix Mono and Stereo sources to 4-channel destinations, we duplicate source
      // channels across multiple destinations (Stereo LR becomes LRLR, Mono M becomes MMMM).
      // Audio formats do not include info needed to filter frequencies or 3D-locate channels.
      // TODO(fxbug.dev/13679): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(fxbug.dev/13682): account for frequency range (e.g. "4-channel" stereo woofer+tweeter)
      return SelectPSM<4>(source_format, dest_format, gain_limits);
    default:
      FX_LOGS(WARNING) << "PointSampler does not support this channelization: "
                       << source_format.channels << " -> " << dest_format.channels;
      return nullptr;
  }
}

}  // namespace media::audio::mixer
