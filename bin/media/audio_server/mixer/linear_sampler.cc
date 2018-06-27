// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/mixer/linear_sampler.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_server/constants.h"
#include "garnet/bin/media/audio_server/mixer/mixer_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace mixer {

// We specify alpha in fixed-point 19.13: a max val of "1.0" is 0x00002000.
inline float Interpolate(float A, float B, uint32_t alpha) {
  double val = static_cast<double>(B - A) * alpha / (1 << kPtsFractionalBits);
  return A + val;
}

template <size_t DChCount, typename SType, size_t SChCount>
class LinearSamplerImpl : public LinearSampler {
 public:
  LinearSamplerImpl() : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1) { Reset(); }

  bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t modulo = 0,
           uint32_t denominator = 1) override;

  void Reset() override { ::memset(filter_data_, 0, sizeof(filter_data_)); }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, uint32_t frac_step_size,
                  uint32_t modulo, uint32_t denominator,
                  Gain::AScale amplitude_scale);

  float filter_data_[2 * DChCount];
};

// TODO(mpuryear): MTWN-75 factor to minimize LinearSamplerImpl code duplication
template <typename SType>
class NxNLinearSamplerImpl : public LinearSampler {
 public:
  NxNLinearSamplerImpl(size_t channelCount)
      : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1), chan_count_(channelCount) {
    filter_data_u_ = std::make_unique<float[]>(2 * chan_count_);
    Reset();
  }

  bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t modulo = 0,
           uint32_t denominator = 1) override;

  void Reset() override {
    ::memset(filter_data_u_.get(), 0,
             2 * chan_count_ * sizeof(filter_data_u_[0]));
  }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dst, uint32_t dst_frames, uint32_t* dst_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, uint32_t frac_step_size,
                  uint32_t modulo, uint32_t denominator,
                  Gain::AScale amplitude_scale, size_t chan_count);

  size_t chan_count_;
  std::unique_ptr<float[]> filter_data_u_;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DChCount, typename SType, size_t SChCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool LinearSamplerImpl<DChCount, SType, SChCount>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    uint32_t modulo, uint32_t denominator, Gain::AScale amplitude_scale) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
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

  // "Source end" is the last valid input sub-frame that can be sampled.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // Otherwise, all these samples are in the future and irrelevant here. Callers
  // explicitly avoid calling Mix in this case, so we have detected an error.
  // For linear_sampler this implies the requirement that soff > -FRAC_ONE.
  FXL_DCHECK(soff + static_cast<int32_t>(pos_filter_width()) >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Otherwise, all these samples are in the past and irrelevant here. Callers
  // explicitly avoid calling Mix in this case, so we have detected an error.
  // For linear_sampler this implies a requirement that soff < frac_src_frames.
  FXL_DCHECK(soff + FRAC_ONE <= frac_src_frames + neg_filter_width());

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets and hold onto
  // any relevant filter data from the end of the source.
  if (ScaleType != ScalerType::MUTED) {
    // When starting "between buffers", we must rely on previously-cached vals.
    if (soff < 0) {
      for (size_t D = 0; D < DChCount; ++D) {
        filter_data_[DChCount + D] = SR::Read(src + (D / SR::DstPerSrc));
      }

      do {
        float* out = dst + (doff * DChCount);

        for (size_t D = 0; D < DChCount; ++D) {
          float sample = Interpolate(
              filter_data_[D], filter_data_[DChCount + D], soff + FRAC_ONE);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
      } while ((doff < dst_frames) && (soff < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((doff < dst_frames) && (soff < src_end)) {
      uint32_t S = (soff >> kPtsFractionalBits) * SChCount;
      float* out = dst + (doff * DChCount);

      for (size_t D = 0; D < DChCount; ++D) {
        float s1 = SR::Read(src + S + (D / SR::DstPerSrc));
        float s2 = SR::Read(src + S + (D / SR::DstPerSrc) + SChCount);
        float sample = Interpolate(s1, s2, soff & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the soff and doff values appropriately.
    if ((doff < dst_frames) && (soff < src_end)) {
      uint32_t src_avail =
          (((src_end - soff) + frac_step_size - 1) / frac_step_size);
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      doff += avail;
      soff += avail * frac_step_size;

      if (HasModulo) {
        source_modulo += (modulo * avail);
        soff += (source_modulo / denominator);
        source_modulo %= denominator;
      }
    }
  }

  // If we have room for at least one more sample, and our sampling position
  // hits the input buffer's final frame exactly ...
  if ((doff < dst_frames) && (soff == src_end)) {
    // ... and if we are not muted, of course ...
    if (ScaleType != ScalerType::MUTED) {
      // ... then we can _point-sample_ one final frame into our output buffer.
      // We need not _interpolate_ since fractional position is exactly zero.
      uint32_t S = (soff >> kPtsFractionalBits) * SChCount;
      float* out = dst + (doff * DChCount);

      for (size_t D = 0; D < DChCount; ++D) {
        float sample = SR::Read(src + S + (D / SR::DstPerSrc));
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
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

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If next source position to consume is beyond start of last frame ...
  if (soff > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * SChCount;
    // ... and if we are not mute, of course...
    if (ScaleType != ScalerType::MUTED) {
      // ... cache our final frame for use in future interpolation ...
      for (size_t D = 0; D < DChCount; ++D) {
        filter_data_[D] = SR::Read(src + S + (D / SR::DstPerSrc));
      }
    } else {
      // ... otherwise cache silence (which is what we actually produced).
      for (size_t D = 0; D < DChCount; ++D) {
        filter_data_[D] = 0;
      }
    }
    // At this point the source offset (soff) is either somewhere within the
    // last source sample, or entirely beyond the end of the source buffer (if
    // frac_step_size is greater than unity).  Either way, we've extracted all
    // of the information from this source buffer, and can return TRUE.
    return true;
  }

  // The source offset (soff) is exactly on the start of the last source sample,
  // or earlier. Thus we have not exhausted this source buffer -- return FALSE.
  return false;
}

template <size_t DChCount, typename SType, size_t SChCount>
bool LinearSamplerImpl<DChCount, SType, SChCount>::Mix(
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
inline bool NxNLinearSamplerImpl<SType>::Mix(
    float* dst, uint32_t dst_frames, uint32_t* dst_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    uint32_t modulo, uint32_t denominator, Gain::AScale amplitude_scale,
    size_t chan_count) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
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

  // This is the last sub-frame at which we can output without additional data.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // For linear_sampler this means that soff > -FRAC_ONE.
  FXL_DCHECK(soff + static_cast<int32_t>(pos_filter_width()) >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  FXL_DCHECK(soff + FRAC_ONE <= frac_src_frames + neg_filter_width());

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets and hold onto
  // any relevant filter data from the end of the source.
  if (ScaleType != ScalerType::MUTED) {
    // When starting "between buffers", we must rely on previously-cached vals.
    if (soff < 0) {
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[chan_count + D] = SampleNormalizer<SType>::Read(src + D);
      }

      do {
        float* out = dst + (doff * chan_count);

        for (size_t D = 0; D < chan_count; ++D) {
          float sample = Interpolate(filter_data_u_[chan_count + D],
                                     filter_data_u_[D], -soff);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
      } while ((doff < dst_frames) && (soff < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((doff < dst_frames) && (soff < src_end)) {
      uint32_t S = (soff >> kPtsFractionalBits) * chan_count;
      float* out = dst + (doff * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float s1 = SampleNormalizer<SType>::Read(src + S + D);
        float s2 = SampleNormalizer<SType>::Read(src + S + D + chan_count);
        float sample = Interpolate(s1, s2, soff & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the soff and doff values appropriately.
    if ((doff < dst_frames) && (soff < src_end)) {
      uint32_t src_avail =
          (((src_end - soff) + frac_step_size - 1) / frac_step_size);
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      doff += avail;
      soff += avail * frac_step_size;

      if (HasModulo) {
        source_modulo += (modulo * avail);
        soff += (source_modulo / denominator);
        source_modulo %= denominator;
      }
    }
  }

  // If we have room for at least one more sample, and our sampling position
  // hits the input buffer's final frame exactly ...
  if ((doff < dst_frames) && (soff == src_end)) {
    // ... and if we are not muted, of course ...
    if (ScaleType != ScalerType::MUTED) {
      // ... then we can _point-sample_ one final frame into our output buffer.
      // We need not _interpolate_ since fractional position is exactly zero.
      uint32_t S = (soff >> kPtsFractionalBits) * chan_count;
      float* out = dst + (doff * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float sample = SampleNormalizer<SType>::Read(src + S + D);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
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

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If next source position to consume is beyond start of last frame ...
  if (soff > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * chan_count;
    // ... and if we are not mute, of course...
    if (ScaleType != ScalerType::MUTED) {
      // ... cache our final frame for use in future interpolation ...
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[D] = SampleNormalizer<SType>::Read(src + S + D);
      }
    } else {
      // ... otherwise cache silence (which is what we actually produced).
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[D] = 0;
      }
    }
    // At this point the source offset (soff) is either somewhere within the
    // last source sample, or entirely beyond the end of the source buffer (if
    // frac_step_size is greater than unity).  Either way, we've extracted all
    // of the information from this source buffer, and can return TRUE.
    return true;
  }

  // The source offset (soff) is exactly on the start of the last source sample,
  // or earlier. Thus we have not exhausted this source buffer -- return FALSE.
  return false;
}

template <typename SType>
bool NxNLinearSamplerImpl<SType>::Mix(
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
// LinearSampler Mixer configurations.
template <size_t DChCount, typename SType, size_t SChCount>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioMediaTypeDetails& src_format,
    const fuchsia::media::AudioMediaTypeDetails& dst_format) {
  return MixerPtr(new LinearSamplerImpl<DChCount, SType, SChCount>());
}

template <size_t DChCount, typename SType>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioMediaTypeDetails& src_format,
    const fuchsia::media::AudioMediaTypeDetails& dst_format) {
  switch (src_format.channels) {
    case 1:
      return SelectLSM<DChCount, SType, 1>(src_format, dst_format);
    case 2:
      return SelectLSM<DChCount, SType, 2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

template <size_t DChCount>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioMediaTypeDetails& src_format,
    const fuchsia::media::AudioMediaTypeDetails& dst_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectLSM<DChCount, uint8_t>(src_format, dst_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectLSM<DChCount, int16_t>(src_format, dst_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectLSM<DChCount, float>(src_format, dst_format);
    default:
      return nullptr;
  }
}

static inline MixerPtr SelectNxNLSM(
    const fuchsia::media::AudioMediaTypeDetails& src_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return MixerPtr(new NxNLinearSamplerImpl<uint8_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return MixerPtr(new NxNLinearSamplerImpl<int16_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return MixerPtr(new NxNLinearSamplerImpl<float>(src_format.channels));
    default:
      return nullptr;
  }
}

MixerPtr LinearSampler::Select(
    const fuchsia::media::AudioMediaTypeDetails& src_format,
    const fuchsia::media::AudioMediaTypeDetails& dst_format) {
  if (src_format.channels == dst_format.channels && src_format.channels > 2) {
    return SelectNxNLSM(src_format);
  }

  switch (dst_format.channels) {
    case 1:
      return SelectLSM<1>(src_format, dst_format);
    case 2:
      return SelectLSM<2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

}  // namespace mixer
}  // namespace audio
}  // namespace media
