// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <limits>

#include "fuchsia/media/cpp/fidl.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"
#include "src/media/audio/audio_core/mixer/position_manager.h"

namespace media::audio::mixer {

// Point Sample Mixer implementation.
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth) {}

  bool Mix(float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr,
           const void* source_void_ptr, uint32_t frac_source_frames,
           int32_t* frac_source_offset_ptr, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr,
                         const void* source_void_ptr, uint32_t frac_source_frames,
                         int32_t* frac_source_offset_ptr, Bookkeeping* info);
};

// TODO(fxbug.dev/13361): refactor to minimize code duplication, or even better eliminate NxN
// implementations altogether, replaced by flexible rechannelization (fxbug.dev/13679).
template <typename SourceSampleType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(uint32_t chan_count)
      : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth), chan_count_(chan_count) {}

  bool Mix(float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr,
           const void* source_void_ptr, uint32_t frac_source_frames,
           int32_t* frac_source_offset_ptr, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr,
                         const void* source_void_ptr, uint32_t frac_source_frames,
                         int32_t* frac_source_offset_ptr, Bookkeeping* info, uint32_t chan_count);
  uint32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool PointSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* source_void_ptr,
    uint32_t frac_source_frames, int32_t* frac_source_offset_ptr, Bookkeeping* info) {
  TRACE_DURATION("audio", "PointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SourceReader<SourceSampleType, SourceChanCount, DestChanCount>;
  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto dest_offset = *dest_offset_ptr;

  const auto* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);

  auto frac_source_offset = *frac_source_offset_ptr;
  const auto source_offset =
      static_cast<uint32_t>(frac_source_offset + kPositiveFilterWidth) >> kPtsFractionalBits;
  auto frames_to_mix = std::min((frac_source_frames >> kPtsFractionalBits) - source_offset,
                                dest_frames - dest_offset);

  if constexpr (ScaleType != ScalerType::MUTED) {
    const auto dest_offset_start = dest_offset;

    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    uint32_t source_sample_idx = source_offset * SourceChanCount;
    float* out = dest_ptr + (dest_offset * DestChanCount);

    while (frames_to_mix--) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_offset - dest_offset_start];
      }

      for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
        float sample = SR::Read(source_ptr + source_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      frac_source_offset += FRAC_ONE;
      ++dest_offset;
      source_sample_idx += SourceChanCount;
      out += DestChanCount;
    }
  } else {
    frac_source_offset += (frames_to_mix * FRAC_ONE);
    dest_offset += frames_to_mix;
  }

  // Update all our returned in-out parameters
  *dest_offset_ptr = dest_offset;
  *frac_source_offset_ptr = frac_source_offset;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_source_offset >= static_cast<int32_t>(frac_source_frames - kPositiveFilterWidth));
}

// Regarding ScalerType::MUTED: in the MUTED specialization, the mixer simply skips over the
// appropriate range in the destination buffer, leaving whatever data is already there. We do not
// take additional effort to clear the buffer if 'accumulate' is set, in fact we ignore it in the
// MUTED case. The caller is responsible for clearing the destination buffer before Mix is initially
// called. DoAccumulate is still valuable in the non-mute case, a saving a read+FADD per sample.
//
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
bool PointSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* source_void_ptr,
    uint32_t frac_source_frames, int32_t* frac_source_offset_ptr, bool accumulate) {
  TRACE_DURATION("audio", "PointSamplerImpl::Mix");

  auto info = &bookkeeping();
  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, frac_source_frames,
                                  frac_source_offset_ptr, pos_filter_width(), info);

  if (info->gain.IsUnity()) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, frac_source_frames,
                                                        frac_source_offset_ptr, info)
                      : Mix<ScalerType::EQ_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                         source_void_ptr, frac_source_frames,
                                                         frac_source_offset_ptr, info);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest_ptr, dest_frames, dest_offset_ptr, source_void_ptr,
                                        frac_source_frames, frac_source_offset_ptr, info);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, frac_source_frames,
                                                       frac_source_offset_ptr, info)
                      : Mix<ScalerType::RAMPING, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, frac_source_frames,
                                                        frac_source_offset_ptr, info);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                      source_void_ptr, frac_source_frames,
                                                      frac_source_offset_ptr, info)
                    : Mix<ScalerType::NE_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, frac_source_frames,
                                                       frac_source_offset_ptr, info);
}

// NxN version of the sample-and-hold resampler, with all other optimizations
template <typename SourceSampleType>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool NxNPointSamplerImpl<SourceSampleType>::Mix(float* dest_ptr, uint32_t dest_frames,
                                                       uint32_t* dest_offset_ptr,
                                                       const void* source_void_ptr,
                                                       uint32_t frac_source_frames,
                                                       int32_t* frac_source_offset_ptr,
                                                       Bookkeeping* info, uint32_t chan_count) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SourceReader<SourceSampleType, 1, 1>;
  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto dest_offset = *dest_offset_ptr;

  const auto* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);

  auto frac_source_offset = *frac_source_offset_ptr;
  const auto source_offset =
      static_cast<uint32_t>(frac_source_offset + kPositiveFilterWidth) >> kPtsFractionalBits;
  auto frames_to_mix = std::min((frac_source_frames >> kPtsFractionalBits) - source_offset,
                                dest_frames - dest_offset);

  if constexpr (ScaleType != ScalerType::MUTED) {
    const auto dest_offset_start = dest_offset;

    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    uint32_t source_sample_idx = source_offset * chan_count;
    float* out = dest_ptr + (dest_offset * chan_count);

    while (frames_to_mix--) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_offset - dest_offset_start];
      }

      for (size_t dest_chan = 0; dest_chan < chan_count; ++dest_chan) {
        float sample = SR::Read(source_ptr + source_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      frac_source_offset += FRAC_ONE;
      ++dest_offset;
      source_sample_idx += chan_count;
      out += chan_count;
    }
  } else {
    frac_source_offset += (frames_to_mix * FRAC_ONE);
    dest_offset += frames_to_mix;
  }

  // Update all our returned in-out parameters
  *dest_offset_ptr = dest_offset;
  *frac_source_offset_ptr = frac_source_offset;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_source_offset >= static_cast<int32_t>(frac_source_frames - kPositiveFilterWidth));
}

template <typename SourceSampleType>
bool NxNPointSamplerImpl<SourceSampleType>::Mix(float* dest_ptr, uint32_t dest_frames,
                                                uint32_t* dest_offset_ptr,
                                                const void* source_void_ptr,
                                                uint32_t frac_source_frames,
                                                int32_t* frac_source_offset_ptr, bool accumulate) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::Mix");

  auto info = &bookkeeping();
  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, frac_source_frames,
                                  frac_source_offset_ptr, pos_filter_width(), info);

  if (info->gain.IsUnity()) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, frac_source_frames,
                                                        frac_source_offset_ptr, info, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                         source_void_ptr, frac_source_frames,
                                                         frac_source_offset_ptr, info, chan_count_);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest_ptr, dest_frames, dest_offset_ptr, source_void_ptr,
                                        frac_source_frames, frac_source_offset_ptr, info,
                                        chan_count_);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, frac_source_frames,
                                                       frac_source_offset_ptr, info, chan_count_)
                      : Mix<ScalerType::RAMPING, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                        source_void_ptr, frac_source_frames,
                                                        frac_source_offset_ptr, info, chan_count_);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                      source_void_ptr, frac_source_frames,
                                                      frac_source_offset_ptr, info, chan_count_)
                    : Mix<ScalerType::NE_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                       source_void_ptr, frac_source_frames,
                                                       frac_source_offset_ptr, info, chan_count_);
}

static inline std::unique_ptr<Mixer> SelectNxNPSM(
    const fuchsia::media::AudioStreamType& source_format) {
  TRACE_DURATION("audio", "SelectNxNPSM");

  if (source_format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    return nullptr;
  }

  switch (source_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return std::make_unique<NxNPointSamplerImpl<uint8_t>>(source_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return std::make_unique<NxNPointSamplerImpl<int16_t>>(source_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return std::make_unique<NxNPointSamplerImpl<int32_t>>(source_format.channels);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return std::make_unique<NxNPointSamplerImpl<float>>(source_format.channels);
    default:
      return nullptr;
  }
}

template <size_t DestChanCount, typename SourceSampleType>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType)");

  switch (source_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SourceSampleType, 1>>();
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SourceSampleType, 2>>();
      }
      break;
    case 3:
      if constexpr (DestChanCount <= 2) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SourceSampleType, 3>>();
      }
      break;
    case 4:
      if constexpr (DestChanCount <= 2) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SourceSampleType, 4>>();
      }
      break;
    default:
      break;
  }
  return nullptr;
}

template <size_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan)");

  switch (source_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectPSM<DestChanCount, uint8_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectPSM<DestChanCount, int16_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectPSM<DestChanCount, int32_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectPSM<DestChanCount, float>(source_format, dest_format);
    default:
      return nullptr;
  }
}

std::unique_ptr<Mixer> PointSampler::Select(const fuchsia::media::AudioStreamType& source_format,
                                            const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "PointSampler::Select");

  if (source_format.frames_per_second != dest_format.frames_per_second) {
    FX_LOGS(WARNING) << "PointSampler source frame rate " << source_format.frames_per_second
                     << " must equal dest frame rate " << dest_format.frames_per_second;
    return nullptr;
  }

  // If num_channels for source and dest are equal and > 2, directly map these one-to-one.
  // TODO(fxbug.dev/13361): eliminate NxN mixers; replace w/ flexible rechannelization (see below).
  if (source_format.channels == dest_format.channels && source_format.channels > 2) {
    return SelectNxNPSM(source_format);
  }

  if ((source_format.channels < 1 || dest_format.channels < 1) || (source_format.channels > 4)) {
    FX_LOGS(WARNING) << "PointSampler does not support this channelization: "
                     << source_format.channels << " -> " << dest_format.channels;
    return nullptr;
  }

  switch (dest_format.channels) {
    case 1:
      return SelectPSM<1>(source_format, dest_format);
    case 2:
      return SelectPSM<2>(source_format, dest_format);
    case 3:
      return SelectPSM<3>(source_format, dest_format);
    case 4:
      // For now, to mix Mono and Stereo sources to 4-channel destinations, we duplicate source
      // channels across multiple destinations (Stereo LR becomes LRLR, Mono M becomes MMMM).
      // Audio formats do not include info needed to filter frequencies or locate channels in 3D
      // space.
      // TODO(fxbug.dev/13679): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(fxbug.dev/13682): account for frequency range (e.g. a "4-channel" stereo
      // woofer+tweeter).
      return SelectPSM<4>(source_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace media::audio::mixer
