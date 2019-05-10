// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/linear_sampler.h"

#include <algorithm>
#include <limits>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"

namespace media::audio::mixer {

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class LinearSamplerImpl : public LinearSampler {
 public:
  LinearSamplerImpl() : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1) {}

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           bool accumulate, Bookkeeping* info) override;

  // If/when Bookkeeping is included in this class, clear src_pos_modulo here.
  void Reset() override { ::memset(filter_data_, 0, sizeof(filter_data_)); }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, Bookkeeping* info);

  float filter_data_[DestChanCount] = {0.0f};
};

// TODO(mpuryear): MTWN-75 factor to minimize LinearSamplerImpl code duplication
template <typename SrcSampleType>
class NxNLinearSamplerImpl : public LinearSampler {
 public:
  NxNLinearSamplerImpl(size_t channelCount)
      : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1), chan_count_(channelCount) {
    filter_data_u_ = std::make_unique<float[]>(chan_count_);

    ::memset(filter_data_u_.get(), 0, chan_count_ * sizeof(filter_data_u_[0]));
  }

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           bool accumulate, Bookkeeping* info) override;

  // If/when Bookkeeping is included in this class, clear src_pos_modulo here.
  void Reset() override {
    ::memset(filter_data_u_.get(), 0, chan_count_ * sizeof(filter_data_u_[0]));
  }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, Bookkeeping* info,
                  size_t chan_count);

  size_t chan_count_;
  std::unique_ptr<float[]> filter_data_u_;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    Bookkeeping* info) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // We express number-of-source-frames as fixed-point 19.13 (to align with
  // src_offset) but the actual number of frames provided is always an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // This method must always be provided at least one source frame.
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);

  using DM = DestMixer<ScaleType, DoAccumulate>;
  uint32_t dest_off = *dest_offset;
  uint32_t dest_off_start = dest_off;  // Only used when ramping.

  // Location of first dest frame to produce must be within the provided buffer.
  FXL_DCHECK(dest_off < dest_frames);

  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;
  int32_t src_off = *frac_src_offset;
  const auto* src = static_cast<const SrcSampleType*>(src_void);

  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // Otherwise, all these src samples are in the future and irrelevant here.
  // Callers explicitly avoid calling Mix in this case, so we have detected an
  // error. For linear_sampler, we require src_off > -FRAC_ONE.
  FXL_DCHECK(src_off + static_cast<int32_t>(pos_filter_width()) >= 0)
      << std::hex << "min allowed: 0x" << -pos_filter_width() << ", src_off: 0x"
      << src_off;
  // src_off cannot exceed our last sampleable subframe. We define this as
  // "Source end": the last subframe for which this Mix call can produce output.
  // Otherwise, all these src samples are in the past and irrelevant here.
  auto src_end = static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(src_off <= src_end)
      << std::hex << "src_off: 0x" << src_off << ", src_end: 0x" << src_end;

  // Cache these locally, in the template specialization that uses them.
  // Only src_pos_modulo needs to be written back before returning.
  uint32_t step_size = info->step_size;
  uint32_t rate_modulo, denominator, src_pos_modulo;
  if constexpr (HasModulo) {
    rate_modulo = info->rate_modulo;
    denominator = info->denominator;
    src_pos_modulo = info->src_pos_modulo;

    FXL_DCHECK(denominator > 0);
    FXL_DCHECK(denominator > rate_modulo);
    FXL_DCHECK(denominator > src_pos_modulo);
  }
  if constexpr (kVerboseRampDebug) {
    FXL_LOG(INFO) << "Linear(" << this
                  << ") Ramping: " << (ScaleType == ScalerType::RAMPING)
                  << ", dest_frames: " << dest_frames
                  << ", dest_off: " << dest_off;
  }
  if constexpr (ScaleType == ScalerType::RAMPING) {
    if (dest_frames > Bookkeeping::kScaleArrLen + dest_off) {
      dest_frames = Bookkeeping::kScaleArrLen + dest_off;
    }
  }

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets and hold onto
  // any relevant filter data from the end of the source.
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    }

    // TODO(mpuryear): optimize the logic below for common-case performance.

    // If src_off is negative, we must incorporate previously-cached samples.
    // Add a new sample, to complete the filter set, and compute the output.
    while ((dest_off < dest_frames) && (src_off < 0)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      float* out = dest + (dest_off * DestChanCount);

      for (size_t D = 0; D < DestChanCount; ++D) {
        float s0 = filter_data_[D];
        float s1 = SR::Read(src + (D / SR::DestPerSrc));

        float sample = LinearInterpolate(s0, s1, src_off + FRAC_ONE);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((dest_off < dest_frames) && (src_off <= src_end)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      uint32_t S = (src_off >> kPtsFractionalBits) * SrcChanCount;
      float* out = dest + (dest_off * DestChanCount);

      for (size_t D = 0; D < DestChanCount; ++D) {
        float sample;
        float s0 = SR::Read(src + S + (D / SR::DestPerSrc));
        if ((src_off & FRAC_MASK) == 0) {
          sample = s0;
        } else {
          float s1 = SR::Read(src + S + (D / SR::DestPerSrc) + SrcChanCount);
          sample = LinearInterpolate(s0, s1, src_off & FRAC_MASK);
        }
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the src_off and dest_off values appropriately.
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

        int32_t prev_src_off = (src_pos_modulo < rate_modulo)
                                   ? (src_off - step_size - 1)
                                   : (src_off - step_size);
        while (prev_src_off > src_end) {
          if (src_pos_modulo < rate_modulo) {
            src_pos_modulo += denominator;
          }

          --dest_off;
          src_off = prev_src_off;
          src_pos_modulo -= rate_modulo;

          prev_src_off = (src_pos_modulo < rate_modulo)
                             ? (src_off - step_size - 1)
                             : (src_off - step_size);
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

  // If next source position to consume is beyond start of last frame ...
  if (src_off > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * SrcChanCount;
    // ... cache our final frame for use in future interpolation ...
    for (size_t D = 0; D < DestChanCount; ++D) {
      if constexpr (ScaleType == ScalerType::MUTED) {
        // ... which, if MUTE, is silence (what we actually produced).
        filter_data_[D] = 0;
      } else {
        filter_data_[D] = SR::Read(src + S + (D / SR::DestPerSrc));
      }
    }

    // At this point the source offset (src_off) is either somewhere within
    // the last source sample, or entirely beyond the end of the source buffer
    // (if frac_step_size is greater than unity).  Either way, we've extracted
    // all of the information from this source buffer, and can return TRUE.
    return true;
  }

  // Source offset (src_off) is at or before the start of the last source
  // sample. We have not exhausted this source buffer -- return FALSE.
  return false;
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
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
  } else if (info->gain.IsRamping()) {
    return accumulate
               ? (hasModulo ? Mix<ScalerType::RAMPING, true, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::RAMPING, true, false>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info))
               : (hasModulo ? Mix<ScalerType::RAMPING, false, true>(
                                  dest, dest_frames, dest_offset, src,
                                  frac_src_frames, frac_src_offset, info)
                            : Mix<ScalerType::RAMPING, false, false>(
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
inline bool NxNLinearSamplerImpl<SrcSampleType>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    Bookkeeping* info, size_t chan_count) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // We express number-of-source-frames as fixed-point 19.13 (to align with
  // src_offset) but the actual number of frames provided is always an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // This method must always be provided at least one source frame.
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);

  using DM = DestMixer<ScaleType, DoAccumulate>;
  uint32_t dest_off = *dest_offset;
  uint32_t dest_off_start = dest_off;  // Only used when ramping

  // Location of first dest frame to produce must be within the provided buffer.
  FXL_DCHECK(dest_off < dest_frames);

  int32_t src_off = *frac_src_offset;
  const auto* src = static_cast<const SrcSampleType*>(src_void);

  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // Otherwise, all these src samples are in the future and irrelevant here.
  // Callers explicitly avoid calling Mix in this case, so we have detected an
  // error. For linear_sampler, we require src_off > -FRAC_ONE.
  FXL_DCHECK(src_off + static_cast<int32_t>(pos_filter_width()) >= 0)
      << std::hex << "src_off: 0x" << src_off;
  // src_off cannot exceed our last sampleable subframe. We define this as
  // "Source end": the last subframe for which this Mix call can produce output.
  // Otherwise, all these src samples are in the past and irrelevant here.
  auto src_end = static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(src_off <= src_end)
      << std::hex << "src_off: 0x" << src_off << ", src_end: 0x" << src_end;

  // Cache these locally, in the template specialization that uses them.
  // Only src_pos_modulo must be written back before returning.
  uint32_t step_size = info->step_size;
  uint32_t rate_modulo, denominator, src_pos_modulo;
  if constexpr (HasModulo) {
    rate_modulo = info->rate_modulo;
    denominator = info->denominator;
    src_pos_modulo = info->src_pos_modulo;

    FXL_DCHECK(denominator > 0);
    FXL_DCHECK(denominator > rate_modulo);
    FXL_DCHECK(denominator > src_pos_modulo);
  }
  if constexpr (kVerboseRampDebug) {
    FXL_LOG(INFO) << "Linear-NxN(" << this
                  << ") Ramping: " << (ScaleType == ScalerType::RAMPING)
                  << ", dest_frames: " << dest_frames
                  << ", dest_off: " << dest_off;
  }
  if constexpr (ScaleType == ScalerType::RAMPING) {
    if (dest_frames > Bookkeeping::kScaleArrLen + dest_off) {
      dest_frames = Bookkeeping::kScaleArrLen + dest_off;
    }
  }

  // If we are not attenuated to the point of being muted, perform the mix.
  // Otherwise, just update the source and dest offsets and cache any relevant
  // filter data from the end of the source.
  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    }

    // TODO(mpuryear): optimize the logic below for common-case performance.

    // If src_off is negative, we must incorporate previously-cached samples.
    // Add a new sample, to complete the filter set, and compute the output.
    while ((dest_off < dest_frames) && (src_off < 0)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      float* out = dest + (dest_off * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float s0 = filter_data_u_[D];
        float s1 = SampleNormalizer<SrcSampleType>::Read(src + D);

        float sample = LinearInterpolate(s0, s1, src_off + FRAC_ONE);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((dest_off < dest_frames) && (src_off <= src_end)) {
      if constexpr (ScaleType == ScalerType::RAMPING) {
        amplitude_scale = info->scale_arr[dest_off - dest_off_start];
      }

      uint32_t S = (src_off >> kPtsFractionalBits) * chan_count;
      float* out = dest + (dest_off * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float sample;
        float s0 = SampleNormalizer<SrcSampleType>::Read(src + S + D);
        if ((src_off & FRAC_MASK) == 0) {
          sample = s0;
        } else {
          float s1 =
              SampleNormalizer<SrcSampleType>::Read(src + S + D + chan_count);
          sample = LinearInterpolate(s0, s1, src_off & FRAC_MASK);
        }
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
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
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the src_off and dest_off values appropriately.
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

        int32_t prev_src_off = (src_pos_modulo < rate_modulo)
                                   ? (src_off - step_size - 1)
                                   : (src_off - step_size);
        while (prev_src_off > src_end) {
          if (src_pos_modulo < rate_modulo) {
            src_pos_modulo += denominator;
          }

          --dest_off;
          src_off = prev_src_off;
          src_pos_modulo -= rate_modulo;

          prev_src_off = (src_pos_modulo < rate_modulo)
                             ? (src_off - step_size - 1)
                             : (src_off - step_size);
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

  // If next source position to consume is beyond start of last frame ...
  if (src_off > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * chan_count;
    // ... cache our final frame for use in future interpolation ...
    for (size_t D = 0; D < chan_count; ++D) {
      if constexpr (ScaleType == ScalerType::MUTED) {
        // ... which, if MUTE, is silence (what we actually produced).
        filter_data_u_[D] = 0;
      } else {
        filter_data_u_[D] = SampleNormalizer<SrcSampleType>::Read(src + S + D);
      }
    }

    // At this point the source offset (src_off) is either somewhere within
    // the last source sample, or entirely beyond the end of the source buffer
    // (if frac_step_size is greater than unity).  Either way, we've extracted
    // all of the information from this source buffer, and can return TRUE.
    return true;
  }

  // Source offset (src_off) is at or before the start of the last source
  // sample. We have not exhausted this source buffer -- return FALSE.
  return false;
}

template <typename SrcSampleType>
bool NxNLinearSamplerImpl<SrcSampleType>::Mix(
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
  } else if (info->gain.IsRamping()) {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::RAMPING, true, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::RAMPING, true, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_))
               : (hasModulo
                      ? Mix<ScalerType::RAMPING, false, true>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info, chan_count_)
                      : Mix<ScalerType::RAMPING, false, false>(
                            dest, dest_frames, dest_offset, src,
                            frac_src_frames, frac_src_offset, info,
                            chan_count_));
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
// LinearSampler Mixer configurations.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
static inline std::unique_ptr<Mixer> SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  return std::make_unique<
      LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>>();
}

template <size_t DestChanCount, typename SrcSampleType>
static inline std::unique_ptr<Mixer> SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  switch (src_format.channels) {
    case 1:
      return SelectLSM<DestChanCount, SrcSampleType, 1>(src_format,
                                                        dest_format);
    case 2:
      return SelectLSM<DestChanCount, SrcSampleType, 2>(src_format,
                                                        dest_format);
    default:
      return nullptr;
  }
}

template <size_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectLSM<DestChanCount, uint8_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectLSM<DestChanCount, int16_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectLSM<DestChanCount, int32_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectLSM<DestChanCount, float>(src_format, dest_format);
    default:
      return nullptr;
  }
}

static inline std::unique_ptr<Mixer> SelectNxNLSM(
    const fuchsia::media::AudioStreamType& src_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return std::make_unique<NxNLinearSamplerImpl<uint8_t>>(
          src_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return std::make_unique<NxNLinearSamplerImpl<int16_t>>(
          src_format.channels);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return std::make_unique<NxNLinearSamplerImpl<int32_t>>(
          src_format.channels);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return std::make_unique<NxNLinearSamplerImpl<float>>(src_format.channels);
    default:
      return nullptr;
  }
}

std::unique_ptr<Mixer> LinearSampler::Select(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  if (src_format.channels == dest_format.channels && src_format.channels > 2) {
    return SelectNxNLSM(src_format);
  }

  switch (dest_format.channels) {
    case 1:
      return SelectLSM<1>(src_format, dest_format);
    case 2:
      return SelectLSM<2>(src_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace media::audio::mixer
