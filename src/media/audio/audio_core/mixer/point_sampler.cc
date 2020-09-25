// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <limits>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"

namespace media::audio::mixer {

// Point Sample Mixer implementation.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
                         uint32_t frac_src_frames, int32_t* frac_src_offset, Bookkeeping* info);
};

// TODO(fxbug.dev/13361): refactor to minimize code duplication, or even better eliminate NxN
// implementations altogether, replaced by flexible rechannelization (fxbug.dev/13679).
template <typename SrcSampleType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(uint32_t chan_count)
      : PointSampler(kPositiveFilterWidth, kNegativeFilterWidth), chan_count_(chan_count) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) override;

 private:
  static constexpr uint32_t kPositiveFilterWidth = FRAC_HALF;
  static constexpr uint32_t kNegativeFilterWidth = FRAC_HALF - 1;

  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
                         uint32_t frac_src_frames, int32_t* frac_src_offset, Bookkeeping* info,
                         uint32_t chan_count);
  uint32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset, Bookkeeping* info) {
  TRACE_DURATION("audio", "PointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  // We express number-of-source-frames as fixed-point 19.13 (to align with src_offset) but the
  // actual number of frames provided is always an integer.
  FX_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32, callers should not
  // exceed int32_t::max().
  FX_DCHECK(frac_src_frames <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // This method must always be provided at least one source frame.
  FX_DCHECK(frac_src_frames >= FRAC_ONE);

  using DM = DestMixer<ScaleType, DoAccumulate>;
  auto dest_off = *dest_offset;
  auto dest_off_start = dest_off;  // Only used when ramping.

  // Location of first dest frame to produce must be within the provided buffer.
  FX_DCHECK(dest_off < dest_frames);

  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;
  auto src_off = *frac_src_offset;
  const auto* src = static_cast<const SrcSampleType*>(src_void);

  // "Source offset" can be negative within the bounds of pos_filter_width. PointSampler has no
  // memory: input frames only affect present/future output. Its "positive filter width" is zero,
  // thus src_off must be non-negative. Callers explicitly avoid calling Mix in this error case.
  FX_DCHECK(src_off + kPositiveFilterWidth >= 0) << std::hex << "src_off: 0x" << src_off;

  // src_off cannot exceed our last sampleable subframe. We define this as "Source end": the last
  // subframe for which this Mix call can produce output. Otherwise, all these src samples are in
  // the past -- they have no impact on any output we would produce here.
  auto src_end = static_cast<int32_t>(frac_src_frames - kPositiveFilterWidth) - 1;
  FX_DCHECK(src_end >= 0);

  // Strictly, src_off should be LESS THAN frac_src_frames. We also allow them to be exactly equal,
  // as this is used to "prime" resamplers that use a significant amount of previously-cached data.
  // When equal, we produce no output frame, but samplers with history will cache the final frames.
  FX_DCHECK(src_off <= static_cast<int32_t>(frac_src_frames))
      << std::hex << "src_off: 0x" << src_off << ", src_end: 0x" << src_end
      << ", frac_src_frames: 0x" << frac_src_frames;
  if (src_off == static_cast<int32_t>(frac_src_frames)) {
    return true;
  }

  // Cache these locally, for the HasModulo specializations that use them. Only src_pos_modulo must
  // be written back before returning.
  auto step_size = info->step_size;
  uint32_t rate_modulo, denominator, src_pos_modulo;
  if constexpr (HasModulo) {
    rate_modulo = info->rate_modulo;
    denominator = info->denominator;
    src_pos_modulo = info->src_pos_modulo;

    FX_DCHECK(denominator > 0);
    FX_DCHECK(denominator > rate_modulo);
    FX_DCHECK(denominator > src_pos_modulo);
  }
  if constexpr (kVerboseRampDebug) {
    FX_LOGS(INFO) << "Point Ramping: " << (ScaleType == ScalerType::RAMPING)
                  << ", dest_frames: " << dest_frames << ", dest_off: " << dest_off;
  }
  if constexpr (ScaleType == ScalerType::RAMPING) {
    if (dest_frames > Mixer::Bookkeeping::kScaleArrLen + dest_off) {
      dest_frames = Mixer::Bookkeeping::kScaleArrLen + dest_off;
    }
  }

  // If we are not attenuated to the Muted point, mix. Else, just update source and dest offsets.
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    }

    while ((dest_off < dest_frames) && (src_off <= src_end)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      uint32_t src_iter = ((src_off + kPositiveFilterWidth) >> kPtsFractionalBits) * SrcChanCount;
      float* out = dest + (dest_off * DestChanCount);

      for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
        float sample = SR::Read(src + src_iter, dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      ++dest_off;
      src_off += step_size;

      if constexpr (HasModulo) {
        src_pos_modulo += rate_modulo;
        if (src_pos_modulo >= denominator) {
          ++src_off;
          src_pos_modulo -= denominator;
        }
      }
    }
  } else {
    // We are muted. Don't mix, but figure out how many samples we WOULD have produced and update
    // the src_off and dest_off values appropriately.
    if ((dest_off < dest_frames) && (src_off <= src_end)) {
      uint32_t src_avail = ((src_end - src_off) / step_size) + 1;
      uint32_t dest_avail = dest_frames - dest_off;
      uint32_t avail = std::min(dest_avail, src_avail);

      src_off += (avail * step_size);
      dest_off += avail;

      if constexpr (HasModulo) {
        uint64_t total_mod = src_pos_modulo + (avail * rate_modulo);
        src_off += (total_mod / denominator);
        src_pos_modulo = total_mod % denominator;

        int32_t prev_src_off =
            (src_pos_modulo < rate_modulo) ? (src_off - step_size - 1) : (src_off - step_size);
        while (prev_src_off > src_end) {
          if (src_pos_modulo < rate_modulo) {
            src_pos_modulo += denominator;
          }

          --dest_off;
          src_off = prev_src_off;
          src_pos_modulo -= rate_modulo;

          prev_src_off =
              (src_pos_modulo < rate_modulo) ? (src_off - step_size - 1) : (src_off - step_size);
        }
      }
    }
  }

  // Update all our returned in-out parameters
  *dest_offset = dest_off;
  *frac_src_offset = src_off;
  if constexpr (HasModulo) {
    info->src_pos_modulo = src_pos_modulo;
  }

  // If we passed the last valid source subframe, then we exhausted this source.
  return (src_off > src_end);
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) {
  TRACE_DURATION("audio", "PointSamplerImpl::Mix");

  auto info = &bookkeeping();
  bool hasModulo = (info->denominator > 0 && info->rate_modulo > 0);

  if (info->gain.IsUnity()) {
    return accumulate
               ? (hasModulo ? Mix<ScalerType::EQ_UNITY, true, true>(dest, dest_frames, dest_offset,
                                                                    src, frac_src_frames,
                                                                    frac_src_offset, info)
                            : Mix<ScalerType::EQ_UNITY, true, false>(dest, dest_frames, dest_offset,
                                                                     src, frac_src_frames,
                                                                     frac_src_offset, info))
               : (hasModulo ? Mix<ScalerType::EQ_UNITY, false, true>(dest, dest_frames, dest_offset,
                                                                     src, frac_src_frames,
                                                                     frac_src_offset, info)
                            : Mix<ScalerType::EQ_UNITY, false, false>(
                                  dest, dest_frames, dest_offset, src, frac_src_frames,
                                  frac_src_offset, info));
  } else if (info->gain.IsSilent()) {
    return (hasModulo
                ? Mix<ScalerType::MUTED, true, true>(dest, dest_frames, dest_offset, src,
                                                     frac_src_frames, frac_src_offset, info)
                : Mix<ScalerType::MUTED, true, false>(dest, dest_frames, dest_offset, src,
                                                      frac_src_frames, frac_src_offset, info));
  } else if (info->gain.IsRamping()) {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::RAMPING, true, true>(dest, dest_frames, dest_offset, src,
                                                             frac_src_frames, frac_src_offset, info)
                      : Mix<ScalerType::RAMPING, true, false>(dest, dest_frames, dest_offset, src,
                                                              frac_src_frames, frac_src_offset,
                                                              info))
               : (hasModulo ? Mix<ScalerType::RAMPING, false, true>(dest, dest_frames, dest_offset,
                                                                    src, frac_src_frames,
                                                                    frac_src_offset, info)
                            : Mix<ScalerType::RAMPING, false, false>(dest, dest_frames, dest_offset,
                                                                     src, frac_src_frames,
                                                                     frac_src_offset, info));
  } else {
    return accumulate
               ? (hasModulo ? Mix<ScalerType::NE_UNITY, true, true>(dest, dest_frames, dest_offset,
                                                                    src, frac_src_frames,
                                                                    frac_src_offset, info)
                            : Mix<ScalerType::NE_UNITY, true, false>(dest, dest_frames, dest_offset,
                                                                     src, frac_src_frames,
                                                                     frac_src_offset, info))
               : (hasModulo ? Mix<ScalerType::NE_UNITY, false, true>(dest, dest_frames, dest_offset,
                                                                     src, frac_src_frames,
                                                                     frac_src_offset, info)
                            : Mix<ScalerType::NE_UNITY, false, false>(
                                  dest, dest_frames, dest_offset, src, frac_src_frames,
                                  frac_src_offset, info));
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <typename SrcSampleType>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool NxNPointSamplerImpl<SrcSampleType>::Mix(float* dest, uint32_t dest_frames,
                                                    uint32_t* dest_offset, const void* src_void,
                                                    uint32_t frac_src_frames,
                                                    int32_t* frac_src_offset, Bookkeeping* info,
                                                    uint32_t chan_count) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::MixInternal");
  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  // We express number-of-source-frames as fixed-point 19.13 (to align with src_offset) but the
  // actual number of frames provided is always an integer.
  FX_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FX_DCHECK(frac_src_frames <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // This method must always be provided at least one source frame.
  FX_DCHECK(frac_src_frames >= FRAC_ONE);

  using DM = DestMixer<ScaleType, DoAccumulate>;
  auto dest_off = *dest_offset;
  auto dest_off_start = dest_off;  // Only used when ramping.

  // Location of first dest frame to produce must be within the provided buffer.
  FX_DCHECK(dest_off < dest_frames);

  auto src_off = *frac_src_offset;
  const auto* src = static_cast<const SrcSampleType*>(src_void);

  // "Source offset" can be negative within the bounds of pos_filter_width. PointSampler has no
  // memory: input frames only affect present/future output. Its "positive filter width" is zero,
  // thus src_off must be non-negative. Callers explicitly avoid calling Mix in this error case.
  FX_DCHECK(src_off + kPositiveFilterWidth >= 0) << std::hex << "src_off: 0x" << src_off;

  // src_off cannot exceed our last sampleable subframe. We define this as "Source end": the last
  // subframe for which this Mix call can produce output. Otherwise, all these src samples are in
  // the past -- they have no impact on any output we would produce here.
  auto src_end = static_cast<int32_t>(frac_src_frames - kPositiveFilterWidth) - 1;
  FX_DCHECK(src_end >= 0);

  // Strictly, src_off should be LESS THAN frac_src_frames. We also allow them to be exactly equal,
  // as this is used to "prime" resamplers that use a significant amount of previously-cached data.
  // When equal, we produce no output frame, but samplers with history will cache the final frames.
  FX_DCHECK(src_off <= static_cast<int32_t>(frac_src_frames))
      << std::hex << "src_off: 0x" << src_off << ", src_end: 0x" << src_end
      << ", frac_src_frames: 0x" << frac_src_frames;
  if (src_off == static_cast<int32_t>(frac_src_frames)) {
    return true;
  }

  // Cache these locally, in the template specialization that uses them. Only src_pos_modulo needs
  // to be written back before returning.
  auto step_size = info->step_size;
  uint32_t rate_modulo, denominator, src_pos_modulo;
  if constexpr (HasModulo) {
    rate_modulo = info->rate_modulo;
    denominator = info->denominator;
    src_pos_modulo = info->src_pos_modulo;

    FX_DCHECK(denominator > 0);
    FX_DCHECK(denominator > rate_modulo);
    FX_DCHECK(denominator > src_pos_modulo);
  }
  if constexpr (kVerboseRampDebug) {
    FX_LOGS(INFO) << "Point-NxN Ramping: " << (ScaleType == ScalerType::RAMPING)
                  << ", dest_frames: " << dest_frames << ", dest_off: " << dest_off;
  }
  if constexpr (ScaleType == ScalerType::RAMPING) {
    if (dest_frames > Mixer::Bookkeeping::kScaleArrLen + dest_off) {
      dest_frames = Mixer::Bookkeeping::kScaleArrLen + dest_off;
    }
  }

  // If we are not attenuated to the point of being muted, perform the mix. Otherwise, just update
  // the source and dest offsets.
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    }

    while ((dest_off < dest_frames) && (src_off <= src_end)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      uint32_t src_iter = ((src_off + kPositiveFilterWidth) >> kPtsFractionalBits) * chan_count;
      float* out = dest + (dest_off * chan_count);

      for (size_t dest_chan = 0; dest_chan < chan_count; ++dest_chan) {
        float sample = SampleNormalizer<SrcSampleType>::Read(src + src_iter + dest_chan);
        out[dest_chan] = DM::Mix(out[dest_chan], sample, amplitude_scale);
      }

      ++dest_off;
      src_off += step_size;

      if constexpr (HasModulo) {
        src_pos_modulo += rate_modulo;
        if (src_pos_modulo >= denominator) {
          ++src_off;
          src_pos_modulo -= denominator;
        }
      }
    }
  } else {
    // We are muted. Don't mix, but figure out how many samples we WOULD have produced and update
    // the src_off and dest_off values appropriately.
    if ((dest_off < dest_frames) && (src_off <= src_end)) {
      uint32_t src_avail = ((src_end - src_off) / step_size) + 1;
      uint32_t dest_avail = dest_frames - dest_off;
      uint32_t avail = std::min(src_avail, dest_avail);

      src_off += (avail * step_size);
      dest_off += avail;

      if constexpr (HasModulo) {
        uint64_t total_mod = src_pos_modulo + (avail * rate_modulo);
        src_off += (total_mod / denominator);
        src_pos_modulo = total_mod % denominator;

        int32_t prev_src_off =
            (src_pos_modulo < rate_modulo) ? (src_off - step_size - 1) : (src_off - step_size);
        while (prev_src_off > src_end) {
          if (src_pos_modulo < rate_modulo) {
            src_pos_modulo += denominator;
          }

          --dest_off;
          src_off = prev_src_off;
          src_pos_modulo -= rate_modulo;

          prev_src_off =
              (src_pos_modulo < rate_modulo) ? (src_off - step_size - 1) : (src_off - step_size);
        }
      }
    }
  }

  // Update all our returned in-out parameters
  *dest_offset = dest_off;
  *frac_src_offset = src_off;
  if constexpr (HasModulo) {
    info->src_pos_modulo = src_pos_modulo;
  }

  // If we passed the last valid source subframe, then we exhausted this source.
  return (src_off > src_end);
}

template <typename SrcSampleType>
bool NxNPointSamplerImpl<SrcSampleType>::Mix(float* dest, uint32_t dest_frames,
                                             uint32_t* dest_offset, const void* src,
                                             uint32_t frac_src_frames, int32_t* frac_src_offset,
                                             bool accumulate) {
  TRACE_DURATION("audio", "NxNPointSamplerImpl::Mix");

  auto info = &bookkeeping();
  bool hasModulo = (info->denominator > 0 && info->rate_modulo > 0);
  if (info->gain.IsUnity()) {
    return accumulate ? (hasModulo ? Mix<ScalerType::EQ_UNITY, true, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::EQ_UNITY, true, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_))
                      : (hasModulo ? Mix<ScalerType::EQ_UNITY, false, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::EQ_UNITY, false, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_));
  } else if (info->gain.IsSilent()) {
    return (hasModulo ? Mix<ScalerType::MUTED, true, true>(dest, dest_frames, dest_offset, src,
                                                           frac_src_frames, frac_src_offset, info,
                                                           chan_count_)
                      : Mix<ScalerType::MUTED, true, false>(dest, dest_frames, dest_offset, src,
                                                            frac_src_frames, frac_src_offset, info,
                                                            chan_count_));
  } else if (info->gain.IsRamping()) {
    return accumulate ? (hasModulo ? Mix<ScalerType::RAMPING, true, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::RAMPING, true, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_))
                      : (hasModulo ? Mix<ScalerType::RAMPING, false, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::RAMPING, false, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_));
  } else {
    return accumulate ? (hasModulo ? Mix<ScalerType::NE_UNITY, true, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::NE_UNITY, true, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_))
                      : (hasModulo ? Mix<ScalerType::NE_UNITY, false, true>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_)
                                   : Mix<ScalerType::NE_UNITY, false, false>(
                                         dest, dest_frames, dest_offset, src, frac_src_frames,
                                         frac_src_offset, info, chan_count_));
  }
}

// Templates used to expand the combinations of possible PointSampler configurations.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType,sChan)");
  return std::make_unique<PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>>();
}

template <size_t DestChanCount, typename SrcSampleType>
static inline std::unique_ptr<Mixer> SelectPSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectPSM(dChan,sType)");

  switch (src_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return SelectPSM<DestChanCount, SrcSampleType, 1>(src_format, dest_format);
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return SelectPSM<DestChanCount, SrcSampleType, 2>(src_format, dest_format);
      }
      break;
    case 3:
      if constexpr (DestChanCount <= 2) {
        return SelectPSM<DestChanCount, SrcSampleType, 3>(src_format, dest_format);
      }
      break;
    case 4:
      if constexpr (DestChanCount <= 2) {
        return SelectPSM<DestChanCount, SrcSampleType, 4>(src_format, dest_format);
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

static inline std::unique_ptr<Mixer> SelectNxNPSM(
    const fuchsia::media::AudioStreamType& src_format) {
  TRACE_DURATION("audio", "SelectNxNPSM");
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

std::unique_ptr<Mixer> PointSampler::Select(const fuchsia::media::AudioStreamType& src_format,
                                            const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "PointSampler::Select");

  // If num_channels for src and dest are equal and > 2, directly map these one-to-one.
  // TODO(fxbug.dev/13361): eliminate the NxN mixers, replacing with flexible rechannelization (see
  // below).
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
