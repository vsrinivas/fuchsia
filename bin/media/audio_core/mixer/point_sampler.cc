// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/point_sampler.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_core/mixer/constants.h"
#include "garnet/bin/media/audio_core/mixer/mixer_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace mixer {

// Point Sample Mixer implementation.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(0, FRAC_ONE - 1) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           bool accumulate, Bookkeeping* info) override;

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dest, uint32_t dest_frames,
                         uint32_t* dest_offset, const void* src,
                         uint32_t frac_src_frames, int32_t* frac_src_offset,
                         Bookkeeping* info);
};

// TODO(mpuryear): MTWN-75 factor to minimize PointSamplerImpl code duplication
template <typename SrcSampleType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(uint32_t chan_count)
      : PointSampler(0, FRAC_ONE - 1), chan_count_(chan_count) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           bool accumulate, Bookkeeping* info) override;

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dest, uint32_t dest_frames,
                         uint32_t* dest_offset, const void* src,
                         uint32_t frac_src_frames, int32_t* frac_src_offset,
                         Bookkeeping* info, uint32_t chan_count);
  uint32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    Bookkeeping* info) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;
  using DM = DestMixer<ScaleType, DoAccumulate>;

  const SrcSampleType* src = static_cast<const SrcSampleType*>(src_void);
  uint32_t dest_off = *dest_offset;
  int32_t src_off = *frac_src_offset;

  if (HasModulo) {
    FXL_DCHECK(info->denominator > 0);
    FXL_DCHECK(info->denominator > info->rate_modulo);
    FXL_DCHECK(info->denominator > info->src_pos_modulo);
  }

  FXL_DCHECK(dest_off < dest_frames);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // Source offset can be negative, but within the bounds of pos_filter_width.
  // PointSampler has no memory: input frames only affect present/future output.
  // That is: its "positive filter width" is zero.
  FXL_DCHECK(src_off >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Neg_filter_width is just shy of FRAC_ONE; src_off can't exceed the buf.
  FXL_DCHECK(src_off < static_cast<int32_t>(frac_src_frames));

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets.
  if (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale = info->gain.GetGainScale();

    while ((dest_off < dest_frames) &&
           (src_off < static_cast<int32_t>(frac_src_frames))) {
      uint32_t src_iter = (src_off >> kPtsFractionalBits) * SrcChanCount;
      float* out = dest + (dest_off * DestChanCount);

      for (size_t dest_iter = 0; dest_iter < DestChanCount; ++dest_iter) {
        float sample = SR::Read(src + src_iter + (dest_iter / SR::DestPerSrc));
        out[dest_iter] = DM::Mix(out[dest_iter], sample, amplitude_scale);
      }

      ++dest_off;
      src_off += info->step_size;

      if (HasModulo) {
        info->src_pos_modulo += info->rate_modulo;
        if (info->src_pos_modulo >= info->denominator) {
          ++src_off;
          info->src_pos_modulo -= info->denominator;
        }
      }
    }
  } else {
    if (dest_off < dest_frames) {
      // Calc how many samples we would've produced; update src_off &
      // dest_off.
      uint32_t src_avail =
          ((frac_src_frames - src_off) + info->step_size - 1) / info->step_size;
      uint32_t dest_avail = (dest_frames - dest_off);
      uint32_t avail = std::min(src_avail, dest_avail);

      src_off += avail * info->step_size;
      dest_off += avail;

      if (HasModulo) {
        info->src_pos_modulo += (info->rate_modulo * avail);
        src_off += (info->src_pos_modulo / info->denominator);
        info->src_pos_modulo %= info->denominator;
      }
    }
  }

  *dest_offset = dest_off;
  *frac_src_offset = src_off;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (src_off >= static_cast<int32_t>(frac_src_frames));
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate,
    Bookkeeping* info) {
  FXL_DCHECK(info != nullptr);

  bool hasModulo = (info->denominator > 0 && info->rate_modulo > 0);

  if (info->gain.IsUnity()) {
    return accumulate
               ? (hasModulo ? Mix<ScalerType::EQ_UNITY, true, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::EQ_UNITY, true, false>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info))
               : (hasModulo ? Mix<ScalerType::EQ_UNITY, false, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::EQ_UNITY, false, false>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info));
  } else if (info->gain.IsSilent()) {
    return (hasModulo ? Mix<ScalerType::MUTED, true, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info)
                      : Mix<ScalerType::MUTED, true, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info));
  } else {
    return accumulate
               ? (hasModulo ? Mix<ScalerType::NE_UNITY, true, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::NE_UNITY, true, false>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info))
               : (hasModulo ? Mix<ScalerType::NE_UNITY, false, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::NE_UNITY, false, false>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info));
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <typename SrcSampleType>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool NxNPointSamplerImpl<SrcSampleType>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    Bookkeeping* info, uint32_t chan_count) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  using DM = DestMixer<ScaleType, DoAccumulate>;

  const SrcSampleType* src = static_cast<const SrcSampleType*>(src_void);
  uint32_t dest_off = *dest_offset;
  int32_t src_off = *frac_src_offset;

  if (HasModulo) {
    FXL_DCHECK(info->denominator > 0);
    FXL_DCHECK(info->denominator > info->rate_modulo);
    FXL_DCHECK(info->denominator > info->src_pos_modulo);
  }

  FXL_DCHECK(dest_off < dest_frames);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // Source offset can be negative, but within the bounds of pos_filter_width.
  // PointSampler has no memory: input frames only affect present/future output.
  // That is: its "positive filter width" is zero.
  FXL_DCHECK(src_off >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Neg_filter_width is less than FRAC_ONE, so src_off can't exceed the buf.
  FXL_DCHECK(src_off < static_cast<int32_t>(frac_src_frames));

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets.
  if (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale = info->gain.GetGainScale();
    while ((dest_off < dest_frames) &&
           (src_off < static_cast<int32_t>(frac_src_frames))) {
      uint32_t src_iter = (src_off >> kPtsFractionalBits) * chan_count;
      float* out = dest + (dest_off * chan_count);

      for (size_t dest_iter = 0; dest_iter < chan_count; ++dest_iter) {
        float sample =
            SampleNormalizer<SrcSampleType>::Read(src + src_iter + dest_iter);
        out[dest_iter] = DM::Mix(out[dest_iter], sample, amplitude_scale);
      }

      dest_off += 1;
      src_off += info->step_size;

      if (HasModulo) {
        info->src_pos_modulo += info->rate_modulo;
        if (info->src_pos_modulo >= info->denominator) {
          ++src_off;
          info->src_pos_modulo -= info->denominator;
        }
      }
    }
  } else {
    if (dest_off < dest_frames) {
      // Calc how many samples we would've produced; update src_off &
      // dest_off.
      uint32_t src_avail =
          ((frac_src_frames - src_off) + info->step_size - 1) / info->step_size;
      uint32_t dest_avail = (dest_frames - dest_off);
      uint32_t avail = std::min(src_avail, dest_avail);

      src_off += avail * info->step_size;
      dest_off += avail;

      if (HasModulo) {
        info->src_pos_modulo += (info->rate_modulo * avail);
        src_off += (info->src_pos_modulo / info->denominator);
        info->src_pos_modulo %= info->denominator;
      }
    }
  }

  *dest_offset = dest_off;
  *frac_src_offset = src_off;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (src_off >= static_cast<int32_t>(frac_src_frames));
}

template <typename SrcSampleType>
bool NxNPointSamplerImpl<SrcSampleType>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate,
    Bookkeeping* info) {
  FXL_DCHECK(info != nullptr);

  bool hasModulo = (info->denominator > 0 && info->rate_modulo > 0);

  if (info->gain.IsUnity()) {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::EQ_UNITY, true, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, true, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_))
               : (hasModulo
                      ? Mix<ScalerType::EQ_UNITY, false, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_));
  } else if (info->gain.IsSilent()) {
    return (hasModulo
                ? Mix<ScalerType::MUTED, true, true>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, info, chan_count_)
                : Mix<ScalerType::MUTED, true, false>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, info, chan_count_));
  } else {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::NE_UNITY, true, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::NE_UNITY, true, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_))
               : (hasModulo
                      ? Mix<ScalerType::NE_UNITY, false, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::NE_UNITY, false, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_));
  }
}

// Templates used to expand all of the different combinations of the possible
// PointSampler Mixer configurations.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  return MixerPtr(
      new PointSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>());
}

template <size_t DestChanCount, typename SrcSampleType>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  switch (src_format.channels) {
    case 1:
      return SelectPSM<DestChanCount, SrcSampleType, 1>(src_format,
                                                        dest_format);
    case 2:
      return SelectPSM<DestChanCount, SrcSampleType, 2>(src_format,
                                                        dest_format);
    default:
      return nullptr;
  }
}

template <size_t DestChanCount>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
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

static inline MixerPtr SelectNxNPSM(
    const fuchsia::media::AudioStreamType& src_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return MixerPtr(new NxNPointSamplerImpl<uint8_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return MixerPtr(new NxNPointSamplerImpl<int16_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return MixerPtr(new NxNPointSamplerImpl<int32_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return MixerPtr(new NxNPointSamplerImpl<float>(src_format.channels));
    default:
      return nullptr;
  }
}

MixerPtr PointSampler::Select(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  if (src_format.channels == dest_format.channels && src_format.channels > 2) {
    return SelectNxNPSM(src_format);
  }

  switch (dest_format.channels) {
    case 1:
      return SelectPSM<1>(src_format, dest_format);
    case 2:
      return SelectPSM<2>(src_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace mixer
}  // namespace audio
}  // namespace media
