// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/generic/mixers/linear_sampler.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_server/constants.h"
#include "garnet/bin/media/audio_server/platform/generic/mixers/mixer_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace mixers {

template <size_t DChCount, typename SType, size_t SChCount>
class LinearSamplerImpl : public LinearSampler {
 public:
  LinearSamplerImpl() : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1) { Reset(); }

  bool Mix(int32_t* dst,
           uint32_t dst_frames,
           uint32_t* dst_offset,
           const void* src,
           uint32_t frac_src_frames,
           int32_t* frac_src_offset,
           uint32_t frac_step_size,
           Gain::AScale amplitude_scale,
           bool accumulate) override;

  void Reset() override { ::memset(filter_data_, 0, sizeof(filter_data_)); }

 private:
  template <ScalerType ScaleType, bool DoAccumulate>
  inline bool Mix(int32_t* dst,
                  uint32_t dst_frames,
                  uint32_t* dst_offset,
                  const void* src,
                  uint32_t frac_src_frames,
                  int32_t* frac_src_offset,
                  uint32_t frac_step_size,
                  Gain::AScale amplitude_scale);

  static inline int32_t Interpolate(int32_t A, int32_t B, uint32_t alpha) {
    // Called very frequently: optimized to 3 add, 1 mult, 2 shift, 1 compare.
    A = (A << kPtsFractionalBits) + (B - A) * static_cast<int32_t>(alpha);
    A += (A >= 0 ? kPtsRoundingVal : kPtsRoundingVal - 1);
    return A >> kPtsFractionalBits;
  }

  int32_t filter_data_[2 * DChCount];
};

// TODO(mpuryear): MTWN-75 factor to minimize LinearSamplerImpl code duplication
template <typename SType>
class NxNLinearSamplerImpl : public LinearSampler {
 public:
  NxNLinearSamplerImpl(size_t channelCount)
      : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1), chan_count_(channelCount) {
    filter_data_u_ = std::make_unique<int32_t[]>(2 * chan_count_);
    Reset();
  }

  bool Mix(int32_t* dst,
           uint32_t dst_frames,
           uint32_t* dst_offset,
           const void* src,
           uint32_t frac_src_frames,
           int32_t* frac_src_offset,
           uint32_t frac_step_size,
           Gain::AScale amplitude_scale,
           bool accumulate) override;

  void Reset() override {
    ::memset(filter_data_u_.get(), 0, 2 * chan_count_ * sizeof(int32_t));
  }

 private:
  template <ScalerType ScaleType, bool DoAccumulate>
  inline bool Mix(int32_t* dst,
                  uint32_t dst_frames,
                  uint32_t* dst_offset,
                  const void* src,
                  uint32_t frac_src_frames,
                  int32_t* frac_src_offset,
                  uint32_t frac_step_size,
                  Gain::AScale amplitude_scale,
                  size_t chan_count);

  static inline int32_t Interpolate(int32_t A, int32_t B, uint32_t alpha) {
    // TODO(mpuryear): MTWN-75 minimize LinearSamplerImpl code duplication.
    // Called very frequently: optimized to 3 add, 1 mult, 2 shift, 1 compare.
    A = (A << kPtsFractionalBits) + (B - A) * static_cast<int32_t>(alpha);
    A += (A >= 0 ? kPtsRoundingVal : kPtsRoundingVal - 1);
    return A >> kPtsFractionalBits;
  }

  size_t chan_count_;
  std::unique_ptr<int32_t[]> filter_data_u_;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DChCount, typename SType, size_t SChCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool LinearSamplerImpl<DChCount, SType, SChCount>::Mix(
    int32_t* dst,
    uint32_t dst_frames,
    uint32_t* dst_offset,
    const void* src_void,
    uint32_t frac_src_frames,
    int32_t* frac_src_offset,
    uint32_t frac_step_size,
    Gain::AScale amplitude_scale) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  using SR = SrcReader<SType, SChCount, DChCount>;
  using DM = DstMixer<ScaleType, DoAccumulate>;
  const SType* src = static_cast<const SType*>(src_void);
  uint32_t doff = *dst_offset;
  int32_t soff = *frac_src_offset;

  // "Source end" is the last valid renderer sub-frame that can be sampled.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // For linear_sampler this means that soff > -FRAC_ONE.
  FXL_DCHECK(soff + pos_filter_width() >= 0);

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
        int32_t* out = dst + (doff * DChCount);

        for (size_t D = 0; D < DChCount; ++D) {
          int32_t sample = Interpolate(
              filter_data_[D], filter_data_[DChCount + D], soff + FRAC_ONE);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
        }

        doff += 1;
        soff += frac_step_size;
      } while ((doff < dst_frames) && (soff < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((doff < dst_frames) && (soff < src_end)) {
      uint32_t S = (soff >> kPtsFractionalBits) * SChCount;
      int32_t* out = dst + (doff * DChCount);

      for (size_t D = 0; D < DChCount; ++D) {
        int32_t s1 = SR::Read(src + S + (D / SR::DstPerSrc));
        int32_t s2 = SR::Read(src + S + (D / SR::DstPerSrc) + SChCount);
        int32_t sample = Interpolate(s1, s2, soff & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }

      doff += 1;
      soff += frac_step_size;
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
      int32_t* out = dst + (doff * DChCount);

      for (size_t D = 0; D < DChCount; ++D) {
        int32_t sample = SR::Read(src + S + (D / SR::DstPerSrc));
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
    }

    doff += 1;
    soff += frac_step_size;
  }

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If the next source position for us to consume is beyond the start of the
  // last frame, cache those samples for use in future interpolation.
  if (soff > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * SChCount;
    for (size_t D = 0; D < DChCount; ++D) {
      filter_data_[D] = SR::Read(src + S + (D / SR::DstPerSrc));
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
    int32_t* dst,
    uint32_t dst_frames,
    uint32_t* dst_offset,
    const void* src,
    uint32_t frac_src_frames,
    int32_t* frac_src_offset,
    uint32_t frac_step_size,
    Gain::AScale amplitude_scale,
    bool accumulate) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale)
                      : Mix<ScalerType::EQ_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale);
  } else if (amplitude_scale <= Gain::MuteThreshold(15)) {
    return Mix<ScalerType::MUTED, true>(dst, dst_frames, dst_offset, src,
                                        frac_src_frames, frac_src_offset,
                                        frac_step_size, amplitude_scale);
  } else {
    return accumulate ? Mix<ScalerType::NE_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale)
                      : Mix<ScalerType::NE_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale);
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <typename SType>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool NxNLinearSamplerImpl<SType>::Mix(int32_t* dst,
                                             uint32_t dst_frames,
                                             uint32_t* dst_offset,
                                             const void* src_void,
                                             uint32_t frac_src_frames,
                                             int32_t* frac_src_offset,
                                             uint32_t frac_step_size,
                                             Gain::AScale amplitude_scale,
                                             size_t chan_count) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  using DM = DstMixer<ScaleType, DoAccumulate>;
  const SType* src = static_cast<const SType*>(src_void);
  uint32_t doff = *dst_offset;
  int32_t soff = *frac_src_offset;

  // "Source end" is the last valid renderer sub-frame that can be sampled.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(doff < dst_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // For linear_sampler this means that soff > -FRAC_ONE.
  FXL_DCHECK(soff + pos_filter_width() >= 0);

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
        int32_t* out = dst + (doff * chan_count);

        for (size_t D = 0; D < chan_count; ++D) {
          int32_t sample = Interpolate(filter_data_u_[chan_count + D],
                                       filter_data_u_[D], -soff);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
        }

        doff += 1;
        soff += frac_step_size;
      } while ((doff < dst_frames) && (soff < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((doff < dst_frames) && (soff < src_end)) {
      uint32_t S = (soff >> kPtsFractionalBits) * chan_count;
      int32_t* out = dst + (doff * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        int32_t s1 = SampleNormalizer<SType>::Read(src + S + D);
        int32_t s2 = SampleNormalizer<SType>::Read(src + S + D + chan_count);
        int32_t sample = Interpolate(s1, s2, soff & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }

      doff += 1;
      soff += frac_step_size;
    }
  } else {
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the soff and doff values appropriately.
    if ((doff < dst_frames) && (soff < src_end)) {
      uint32_t src_avail =
          (((src_end - soff) + frac_step_size - 1) / frac_step_size);
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      soff += avail * frac_step_size;
      doff += avail;
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
      int32_t* out = dst + (doff * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        int32_t sample = SampleNormalizer<SType>::Read(src + S + D);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
    }

    doff += 1;
    soff += frac_step_size;
  }

  *dst_offset = doff;
  *frac_src_offset = soff;

  // If the next source position for us to consume is beyond the start of the
  // last frame, cache those samples for use in future interpolation.
  if (soff > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * chan_count;
    for (size_t D = 0; D < chan_count; ++D) {
      filter_data_u_[D] = SampleNormalizer<SType>::Read(src + S + D);
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
bool NxNLinearSamplerImpl<SType>::Mix(int32_t* dst,
                                      uint32_t dst_frames,
                                      uint32_t* dst_offset,
                                      const void* src,
                                      uint32_t frac_src_frames,
                                      int32_t* frac_src_offset,
                                      uint32_t frac_step_size,
                                      Gain::AScale amplitude_scale,
                                      bool accumulate) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale,
                            chan_count_)
                      : Mix<ScalerType::EQ_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale,
                            chan_count_);
  } else if (amplitude_scale <= Gain::MuteThreshold(15)) {
    return Mix<ScalerType::MUTED, true>(
        dst, dst_frames, dst_offset, src, frac_src_frames, frac_src_offset,
        frac_step_size, amplitude_scale, chan_count_);
  } else {
    return accumulate ? Mix<ScalerType::NE_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale,
                            chan_count_)
                      : Mix<ScalerType::NE_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale,
                            chan_count_);
  }
}

// Templates used to expand all of the different combinations of the possible
// Linear Sampler Mixer configurations.
template <size_t DChCount, typename SType, size_t SChCount>
static inline MixerPtr SelectLSM(const AudioMediaTypeDetails& src_format,
                                 const AudioMediaTypeDetails& dst_format) {
  return MixerPtr(new LinearSamplerImpl<DChCount, SType, SChCount>());
}

template <size_t DChCount, typename SType>
static inline MixerPtr SelectLSM(const AudioMediaTypeDetails& src_format,
                                 const AudioMediaTypeDetails& dst_format) {
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
static inline MixerPtr SelectLSM(const AudioMediaTypeDetails& src_format,
                                 const AudioMediaTypeDetails& dst_format) {
  switch (src_format.sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      return SelectLSM<DChCount, uint8_t>(src_format, dst_format);
    case AudioSampleFormat::SIGNED_16:
      return SelectLSM<DChCount, int16_t>(src_format, dst_format);
    case AudioSampleFormat::FLOAT:
      return SelectLSM<DChCount, float>(src_format, dst_format);
    default:
      return nullptr;
  }
}

static inline MixerPtr SelectNxNLSM(const AudioMediaTypeDetails& src_format) {
  switch (src_format.sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      return MixerPtr(new NxNLinearSamplerImpl<uint8_t>(src_format.channels));
    case AudioSampleFormat::SIGNED_16:
      return MixerPtr(new NxNLinearSamplerImpl<int16_t>(src_format.channels));
    case AudioSampleFormat::FLOAT:
      return MixerPtr(new NxNLinearSamplerImpl<float>(src_format.channels));
    default:
      return nullptr;
  }
}

MixerPtr LinearSampler::Select(const AudioMediaTypeDetails& src_format,
                               const AudioMediaTypeDetails& dst_format) {
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

}  // namespace mixers
}  // namespace audio
}  // namespace media
