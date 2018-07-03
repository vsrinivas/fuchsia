// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/mixer/point_sampler.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_server/constants.h"
#include "garnet/bin/media/audio_server/mixer/mixer_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace mixer {

// Point Sample Mixer implementation.
template <size_t DChCount, typename SType, size_t SChCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(0, FRAC_ONE - 1) {}

  bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t modulo = 0,
           uint32_t denominator = 1) override;

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
                         const void* src, uint32_t frac_src_frames,
                         int32_t* frac_src_offset, uint32_t frac_step_size,
                         uint32_t modulo, uint32_t denominator,
                         Gain::AScale amplitude_scale);
};

// TODO(mpuryear): MTWN-75 factor to minimize PointSamplerImpl code duplication
template <typename SType>
class NxNPointSamplerImpl : public PointSampler {
 public:
  NxNPointSamplerImpl(uint32_t chan_count)
      : PointSampler(0, FRAC_ONE - 1), chan_count_(chan_count) {}

  bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t modulo = 0,
           uint32_t denominator = 1) override;

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  static inline bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
                         const void* src, uint32_t frac_src_frames,
                         int32_t* frac_src_offset, uint32_t frac_step_size,
                         uint32_t modulo, uint32_t denominator,
                         Gain::AScale amplitude_scale, uint32_t chan_count);
  uint32_t chan_count_ = 0;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DChCount, typename SType, size_t SChCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool PointSamplerImpl<DChCount, SType, SChCount>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    uint32_t modulo, uint32_t denominator, Gain::AScale amplitude_scale) {
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

  using SR = SrcReader<SType, SChCount, DChCount>;
  using DM = DstMixer<ScaleType, DoAccumulate>;

  const SType* src = static_cast<const SType*>(src_void);
  uint32_t doff = *dst_offset;
  int32_t soff = *frac_src_offset;

  FXL_DCHECK(denominator > 0);
  FXL_DCHECK(denominator > modulo);
  uint32_t source_modulo = 0;

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // Source offset can be negative, but within the bounds of pos_filter_width.
  // PointSampler has no memory: input frames only affect present/future output.
  // That is: its "positive filter width" is zero.
  FXL_DCHECK(soff >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Our neg_filter_width is just shy of FRAC_ONE; soff can't exceed this buf.
  FXL_DCHECK(soff < static_cast<int32_t>(frac_src_frames));

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets.
  if (ScaleType != ScalerType::MUTED) {
    while ((doff < dst_frames) &&
           (soff < static_cast<int32_t>(frac_src_frames))) {
      uint32_t src_iter = (soff >> kPtsFractionalBits) * SChCount;
      float* out = dst + (doff * DChCount);

      for (size_t dst_iter = 0; dst_iter < DChCount; ++dst_iter) {
        float sample = SR::Read(src + src_iter + (dst_iter / SR::DstPerSrc));
        out[dst_iter] = DM::Mix(out[dst_iter], sample, amplitude_scale);
      }

      ++doff;
      soff += frac_step_size;

      if (HasModulo) {
        source_modulo += modulo;
        if (source_modulo >= denominator) {
          ++soff;
          source_modulo -= denominator;
        }
      }
    }
  } else {
    if (doff < dst_frames) {
      // Calc how many samples we would have produced; update soff and doff.
      uint32_t src_avail =
          ((frac_src_frames - soff) + frac_step_size - 1) / frac_step_size;
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      soff += avail * frac_step_size;
      doff += avail;

      if (HasModulo) {
        source_modulo += (modulo * avail);
        soff += (source_modulo / denominator);
        source_modulo %= denominator;
      }
    }
  }

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (soff >= static_cast<int32_t>(frac_src_frames));
}

template <size_t DChCount, typename SType, size_t SChCount>
bool PointSamplerImpl<DChCount, SType, SChCount>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    Gain::AScale amplitude_scale, bool accumulate, uint32_t modulo,
    uint32_t denominator) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate
               ? (modulo > 0
                      ? Mix<ScalerType::EQ_UNITY, true, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale)
                      : Mix<ScalerType::EQ_UNITY, true, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale))
               : (modulo > 0
                      ? Mix<ScalerType::EQ_UNITY, false, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale)
                      : Mix<ScalerType::EQ_UNITY, false, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale));
  } else if (amplitude_scale <= Gain::MuteThreshold()) {
    return (modulo > 0 ? Mix<ScalerType::MUTED, true, true>(
                             dst, dst_frames, dst_offset, src, frac_src_frames,
                             frac_src_offset, frac_step_size, modulo,
                             denominator, amplitude_scale)
                       : Mix<ScalerType::MUTED, true, false>(
                             dst, dst_frames, dst_offset, src, frac_src_frames,
                             frac_src_offset, frac_step_size, modulo,
                             denominator, amplitude_scale));
  } else {
    return accumulate
               ? (modulo > 0
                      ? Mix<ScalerType::NE_UNITY, true, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale)
                      : Mix<ScalerType::NE_UNITY, true, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale))
               : (modulo > 0
                      ? Mix<ScalerType::NE_UNITY, false, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale)
                      : Mix<ScalerType::NE_UNITY, false, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale));
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <typename SType>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool NxNPointSamplerImpl<SType>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    uint32_t modulo, uint32_t denominator, Gain::AScale amplitude_scale,
    uint32_t chan_count) {
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

  using DM = DstMixer<ScaleType, DoAccumulate>;

  const SType* src = static_cast<const SType*>(src_void);
  uint32_t doff = *dst_offset;
  int32_t soff = *frac_src_offset;

  FXL_DCHECK(denominator > 0);
  FXL_DCHECK(denominator > modulo);
  uint32_t source_modulo = 0;

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // Source offset can be negative, but within the bounds of pos_filter_width.
  // PointSampler has no memory: input frames only affect present/future output.
  // That is: its "positive filter width" is zero.
  FXL_DCHECK(soff >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Our neg_filter_width is just shy of FRAC_ONE; soff can't exceed this buf.
  FXL_DCHECK(soff < static_cast<int32_t>(frac_src_frames));

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets.
  if (ScaleType != ScalerType::MUTED) {
    while ((doff < dst_frames) &&
           (soff < static_cast<int32_t>(frac_src_frames))) {
      uint32_t src_iter = (soff >> kPtsFractionalBits) * chan_count;
      float* out = dst + (doff * chan_count);

      for (size_t dst_iter = 0; dst_iter < chan_count; ++dst_iter) {
        float sample = SampleNormalizer<SType>::Read(src + src_iter + dst_iter);
        out[dst_iter] = DM::Mix(out[dst_iter], sample, amplitude_scale);
      }

      doff += 1;
      soff += frac_step_size;

      if (HasModulo) {
        source_modulo += modulo;
        if (source_modulo >= denominator) {
          ++soff;
          source_modulo -= denominator;
        }
      }
    }
  } else {
    if (doff < dst_frames) {
      // Figure out how many samples we would have produced and update the
      // soff and doff values appropriately.
      uint32_t src_avail =
          ((frac_src_frames - soff) + frac_step_size - 1) / frac_step_size;
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      soff += avail * frac_step_size;
      doff += avail;

      if (HasModulo) {
        source_modulo += (modulo * avail);
        soff += (source_modulo / denominator);
        source_modulo %= denominator;
      }
    }
  }

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If we passed the last valid source subframe, then we exhausted this source.
  return (soff >= static_cast<int32_t>(frac_src_frames));
}

template <typename SType>
bool NxNPointSamplerImpl<SType>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    Gain::AScale amplitude_scale, bool accumulate, uint32_t modulo,
    uint32_t denominator) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate
               ? (modulo > 0
                      ? Mix<ScalerType::EQ_UNITY, true, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, true, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_))
               : (modulo > 0
                      ? Mix<ScalerType::EQ_UNITY, false, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_));
  } else if (amplitude_scale <= Gain::MuteThreshold()) {
    return (modulo > 0 ? Mix<ScalerType::MUTED, true, true>(
                             dst, dst_frames, dst_offset, src, frac_src_frames,
                             frac_src_offset, frac_step_size, modulo,
                             denominator, amplitude_scale, chan_count_)
                       : Mix<ScalerType::MUTED, true, false>(
                             dst, dst_frames, dst_offset, src, frac_src_frames,
                             frac_src_offset, frac_step_size, modulo,
                             denominator, amplitude_scale, chan_count_));
  } else {
    return accumulate
               ? (modulo > 0
                      ? Mix<ScalerType::NE_UNITY, true, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_)
                      : Mix<ScalerType::NE_UNITY, true, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_))
               : (modulo > 0
                      ? Mix<ScalerType::NE_UNITY, false, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_)
                      : Mix<ScalerType::NE_UNITY, false, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, modulo,
                            denominator, amplitude_scale, chan_count_));
  }
}

// Templates used to expand all of the different combinations of the possible
// PointSampler Mixer configurations.
template <size_t DChCount, typename SType, size_t SChCount>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dst_format) {
  return MixerPtr(new PointSamplerImpl<DChCount, SType, SChCount>());
}

template <size_t DChCount, typename SType>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dst_format) {
  switch (src_format.channels) {
    case 1:
      return SelectPSM<DChCount, SType, 1>(src_format, dst_format);
    case 2:
      return SelectPSM<DChCount, SType, 2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

template <size_t DChCount>
static inline MixerPtr SelectPSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dst_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectPSM<DChCount, uint8_t>(src_format, dst_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectPSM<DChCount, int16_t>(src_format, dst_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectPSM<DChCount, int32_t>(src_format, dst_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectPSM<DChCount, float>(src_format, dst_format);
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
    const fuchsia::media::AudioStreamType& dst_format) {
  if (src_format.channels == dst_format.channels && src_format.channels > 2) {
    return SelectNxNPSM(src_format);
  }

  switch (dst_format.channels) {
    case 1:
      return SelectPSM<1>(src_format, dst_format);
    case 2:
      return SelectPSM<2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

}  // namespace mixer
}  // namespace audio
}  // namespace media
