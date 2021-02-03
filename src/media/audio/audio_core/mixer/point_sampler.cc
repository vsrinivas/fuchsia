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
#include "src/media/audio/lib/format/constants.h"

namespace media::audio::mixer {

// Point Sample Mixer implementation.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset_ptr, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr,
                         const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset_ptr,
                         Bookkeeping* info);
};

// TODO(fxbug.dev/13361): refactor to minimize code duplication, or even better eliminate NxN
// implementations altogether, replaced by flexible rechannelization (fxbug.dev/13679).
template <typename SrcSampleType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(uint32_t chan_count)
      : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth), chan_count_(chan_count) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset_ptr, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr,
                         const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset_ptr,
                         Bookkeeping* info, uint32_t chan_count);
  uint32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset_ptr, Bookkeeping* info) {
  TRACE_DURATION("audio", "PointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;
  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto dest_off = *dest_offset_ptr;
  FX_CHECK(dest_off < dest_frames);

  FX_CHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FX_CHECK(frac_src_frames <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  auto frac_src_offset = *frac_src_offset_ptr;
  FX_CHECK(frac_src_offset + kPositiveFilterWidth >= 0)
      << std::hex << "frac_src_offset: 0x" << frac_src_offset;
  FX_CHECK(frac_src_offset <= static_cast<int32_t>(frac_src_frames))
      << std::hex << "frac_src_offset: 0x" << frac_src_offset << ", frac_src_frames: 0x"
      << frac_src_frames;

  const auto* src = static_cast<const SrcSampleType*>(src_void);

  const auto src_offset =
      static_cast<uint32_t>(frac_src_offset + kPositiveFilterWidth) >> kPtsFractionalBits;
  auto frames_to_mix =
      std::min((frac_src_frames >> kPtsFractionalBits) - src_offset, dest_frames - dest_off);

  if constexpr (ScaleType != ScalerType::MUTED) {
    const auto dest_off_start = dest_off;

    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    uint32_t src_sample_idx = src_offset * SrcChanCount;
    float* out = dest + (dest_off * DestChanCount);

    while (frames_to_mix--) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
        float sample = SR::Read(src + src_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      frac_src_offset += FRAC_ONE;
      ++dest_off;
      src_sample_idx += SrcChanCount;
      out += DestChanCount;
    }
  } else {
    frac_src_offset += (frames_to_mix * FRAC_ONE);
    dest_off += frames_to_mix;
  }

  // Update all our returned in-out parameters
  *dest_offset_ptr = dest_off;
  *frac_src_offset_ptr = frac_src_offset;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_src_offset >= static_cast<int32_t>(frac_src_frames - kPositiveFilterWidth));
}

// Regarding ScalerType::MUTED: in the MUTED specialization, the mixer simply skips over the
// appropriate range in the destination buffer, leaving whatever data is already there. We do not
// take additional effort to clear the buffer if 'accumulate' is set, in fact we ignore it in the
// MUTED case. The caller is responsible for clearing the destination buffer before Mix is initially
// called. DoAccumulate is still valuable in the non-mute case, a saving a read+FADD per sample.
//
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset_ptr, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset_ptr, bool accumulate) {
  TRACE_DURATION("audio", "PointSamplerImpl::Mix");

  auto info = &bookkeeping();

  if (info->gain.IsUnity()) {
    return accumulate
               ? Mix<ScalerType::EQ_UNITY, true>(dest, dest_frames, dest_offset_ptr, src,
                                                 frac_src_frames, frac_src_offset_ptr, info)
               : Mix<ScalerType::EQ_UNITY, false>(dest, dest_frames, dest_offset_ptr, src,
                                                  frac_src_frames, frac_src_offset_ptr, info);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest, dest_frames, dest_offset_ptr, src, frac_src_frames,
                                        frac_src_offset_ptr, info);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest, dest_frames, dest_offset_ptr, src,
                                                       frac_src_frames, frac_src_offset_ptr, info)
                      : Mix<ScalerType::RAMPING, false>(dest, dest_frames, dest_offset_ptr, src,
                                                        frac_src_frames, frac_src_offset_ptr, info);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest, dest_frames, dest_offset_ptr, src,
                                                      frac_src_frames, frac_src_offset_ptr, info)
                    : Mix<ScalerType::NE_UNITY, false>(dest, dest_frames, dest_offset_ptr, src,
                                                       frac_src_frames, frac_src_offset_ptr, info);
}

// NxN version of the sample-and-hold resampler, with all other optimizations
template <typename SrcSampleType>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool NxNPointSamplerImpl<SrcSampleType>::Mix(float* dest, uint32_t dest_frames,
                                                    uint32_t* dest_offset_ptr, const void* src_void,
                                                    uint32_t frac_src_frames,
                                                    int32_t* frac_src_offset_ptr, Bookkeeping* info,
                                                    uint32_t chan_count) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SrcReader<SrcSampleType, 1, 1>;
  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto dest_off = *dest_offset_ptr;
  FX_CHECK(dest_off < dest_frames);

  FX_CHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FX_CHECK(frac_src_frames <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  auto frac_src_offset = *frac_src_offset_ptr;
  FX_CHECK(frac_src_offset + kPositiveFilterWidth >= 0)
      << std::hex << "frac_src_offset: 0x" << frac_src_offset;
  FX_CHECK(frac_src_offset <= static_cast<int32_t>(frac_src_frames))
      << std::hex << "frac_src_offset: 0x" << frac_src_offset << ", frac_src_frames: 0x"
      << frac_src_frames;

  const auto* src = static_cast<const SrcSampleType*>(src_void);

  const auto src_offset =
      static_cast<uint32_t>(frac_src_offset + kPositiveFilterWidth) >> kPtsFractionalBits;
  auto frames_to_mix =
      std::min((frac_src_frames >> kPtsFractionalBits) - src_offset, dest_frames - dest_off);

  if constexpr (ScaleType != ScalerType::MUTED) {
    const auto dest_off_start = dest_off;

    Gain::AScale amplitude_scale = Gain::kUnityScale;
    if constexpr (ScaleType == ScalerType::NE_UNITY) {
      amplitude_scale = info->gain.GetGainScale();
    }

    uint32_t src_sample_idx = src_offset * chan_count;
    float* out = dest + (dest_off * chan_count);

    while (frames_to_mix--) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      for (size_t dest_chan = 0; dest_chan < chan_count; ++dest_chan) {
        float sample = SR::Read(src + src_sample_idx, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      frac_src_offset += FRAC_ONE;
      ++dest_off;
      src_sample_idx += chan_count;
      out += chan_count;
    }
  } else {
    frac_src_offset += (frames_to_mix * FRAC_ONE);
    dest_off += frames_to_mix;
  }

  // Update all our returned in-out parameters
  *dest_offset_ptr = dest_off;
  *frac_src_offset_ptr = frac_src_offset;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (frac_src_offset >= static_cast<int32_t>(frac_src_frames - kPositiveFilterWidth));
}

template <typename SrcSampleType>
bool NxNPointSamplerImpl<SrcSampleType>::Mix(float* dest, uint32_t dest_frames,
                                             uint32_t* dest_offset_ptr, const void* src,
                                             uint32_t frac_src_frames, int32_t* frac_src_offset_ptr,
                                             bool accumulate) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::Mix");

  auto info = &bookkeeping();

  if (info->gain.IsUnity()) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(dest, dest_frames, dest_offset_ptr, src,
                                                        frac_src_frames, frac_src_offset_ptr, info,
                                                        chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false>(dest, dest_frames, dest_offset_ptr, src,
                                                         frac_src_frames, frac_src_offset_ptr, info,
                                                         chan_count_);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest, dest_frames, dest_offset_ptr, src, frac_src_frames,
                                        frac_src_offset_ptr, info, chan_count_);
  }

  if (info->gain.IsRamping()) {
    dest_frames = std::min(dest_frames, *dest_offset_ptr + Bookkeeping::kScaleArrLen);
    return accumulate ? Mix<ScalerType::RAMPING, true>(dest, dest_frames, dest_offset_ptr, src,
                                                       frac_src_frames, frac_src_offset_ptr, info,
                                                       chan_count_)
                      : Mix<ScalerType::RAMPING, false>(dest, dest_frames, dest_offset_ptr, src,
                                                        frac_src_frames, frac_src_offset_ptr, info,
                                                        chan_count_);
  }

  return accumulate ? Mix<ScalerType::NE_UNITY, true>(dest, dest_frames, dest_offset_ptr, src,
                                                      frac_src_frames, frac_src_offset_ptr, info,
                                                      chan_count_)
                    : Mix<ScalerType::NE_UNITY, false>(dest, dest_frames, dest_offset_ptr, src,
                                                       frac_src_frames, frac_src_offset_ptr, info,
                                                       chan_count_);
}

static inline std::unique_ptr<Mixer> SelectNxNPSM(
    const fuchsia::media::AudioStreamType& src_format) {
  TRACE_DURATION("audio", "SelectNxNPSM");

  if (src_format.channels > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    return nullptr;
  }

  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return std::make_unique<NxNPointSamplerImpl<uint8_t>>(src_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return std::make_unique<NxNPointSamplerImpl<int16_t>>(src_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return std::make_unique<NxNPointSamplerImpl<int32_t>>(src_format.channels);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return std::make_unique<NxNPointSamplerImpl<float>>(src_format.channels);
    default:
      return nullptr;
  }
}

template <size_t DestChanCount, typename SrcSampleType>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType)");

  switch (src_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SrcSampleType, 1>>();
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SrcSampleType, 2>>();
      }
      break;
    case 3:
      if constexpr (DestChanCount <= 2) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SrcSampleType, 3>>();
      }
      break;
    case 4:
      if constexpr (DestChanCount <= 2) {
        return std::make_unique<PointSamplerImpl<DestChanCount, SrcSampleType, 4>>();
      }
      break;
    default:
      break;
  }
  return nullptr;
}

template <size_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan)");

  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectPSM<DestChanCount, uint8_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectPSM<DestChanCount, int16_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectPSM<DestChanCount, int32_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectPSM<DestChanCount, float>(src_format, dest_format);
    default:
      return nullptr;
  }
}

std::unique_ptr<Mixer> PointSampler::Select(const fuchsia::media::AudioStreamType& src_format,
                                            const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "PointSampler::Select");

  if (src_format.frames_per_second != dest_format.frames_per_second) {
    return nullptr;
  }

  // If num_channels for src and dest are equal and > 2, directly map these one-to-one.
  // TODO(fxbug.dev/13361): eliminate the NxN mixers, replacing with flexible rechannelization
  // (see below).
  if (src_format.channels == dest_format.channels && src_format.channels > 2) {
    return SelectNxNPSM(src_format);
  }

  if ((src_format.channels < 1 || dest_format.channels < 1) ||
      (src_format.channels > 4 || dest_format.channels > 4)) {
    return nullptr;
  }

  switch (dest_format.channels) {
    case 1:
      return SelectPSM<1>(src_format, dest_format);
    case 2:
      return SelectPSM<2>(src_format, dest_format);
    case 3:
      return SelectPSM<3>(src_format, dest_format);
    case 4:
      // For now, to mix Mono and Stereo sources to 4-channel destinations, we duplicate source
      // channels across multiple destinations (Stereo LR becomes LRLR, Mono M becomes MMMM).
      // Audio formats do not include info needed to filter frequencies or locate channels in 3D
      // space.
      // TODO(fxbug.dev/13679): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(fxbug.dev/13682): account for frequency range (e.g. a "4-channel" stereo
      // woofer+tweeter).
      return SelectPSM<4>(src_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace media::audio::mixer
