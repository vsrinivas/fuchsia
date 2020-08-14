// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/sinc_sampler.h"

#include <lib/trace/event.h>

#include <algorithm>
#include <limits>

#include "src/media/audio/audio_core/mixer/channel_strip.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/filter.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"
#include "src/media/audio/audio_core/mixer/position_manager.h"

namespace media::audio::mixer {

// Note that this value directly determines the maximum downsampling ratio: the ratio's numerator is
// (kDataCacheLength/28). For example, if kDataCacheLength is 280, max downsampling ratio is 10:1.
//
// Using 'audio_fidelity_tests --profile', the performance of various lengths was measured. The
// length 680 had better performance than other measured lengths (280, 560, 640, 700, 720, 1000,
// 1344), presumably because of cache/locality effects. This length allows a downsampling ratio
// greater than 24:1 -- even with 192kHz input hardware, we can produce 8kHz streams to capturers.
static constexpr size_t kDataCacheLength = 680;

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class SincSamplerImpl : public SincSampler {
 public:
  SincSamplerImpl(uint32_t source_frame_rate, uint32_t dest_frame_rate)
      : SincSampler(SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate),
                    SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate)),
        source_rate_(source_frame_rate),
        dest_rate_(dest_frame_rate),
        position_(SrcChanCount, DestChanCount,
                  SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate),
                  SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate)),
        working_data_(DestChanCount, kDataCacheLength),
        filter_(source_rate_, dest_rate_,
                SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate)) {
    num_prev_frames_needed_ = RightIdx(neg_filter_width().raw_value());
    total_frames_needed_ = num_prev_frames_needed_ + RightIdx(pos_filter_width().raw_value());

    FX_DCHECK(kDataCacheLength > total_frames_needed_)
        << "source rate " << source_frame_rate << ", dest rate " << dest_frame_rate;
  }

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
           uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) override;

  void Reset() override {
    SincSampler::Reset();
    working_data_.Clear();
  }

  virtual void EagerlyPrepare() override { filter_.EagerlyPrepare(); }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
                  uint32_t frac_src_frames, int32_t* frac_src_offset);

  static inline void PopulateFramesToChannelStrip(const void* src_void,
                                                  int32_t next_src_idx_to_copy,
                                                  const uint32_t frames_needed,
                                                  ChannelStrip* channel_strip,
                                                  uint32_t next_cache_idx_to_fill);

  static inline int32_t LeftIdx(int32_t frac_src_offset) {
    return frac_src_offset >> kPtsFractionalBits;
  }
  static inline int32_t RightIdx(int32_t frac_src_offset) {
    return ((frac_src_offset - 1) >> kPtsFractionalBits) + 1;
  }

  uint32_t source_rate_;
  uint32_t dest_rate_;
  uint32_t num_prev_frames_needed_;
  uint32_t total_frames_needed_;

  PositionManager position_;
  ChannelStrip working_data_;
  SincFilter filter_;
};

// Implementation
//
// static
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
inline void
SincSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::PopulateFramesToChannelStrip(
    const void* src_void, int32_t next_src_idx_to_copy, const uint32_t frames_needed,
    ChannelStrip* channel_strip, uint32_t next_cache_idx_to_fill) {
  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;

  const SrcSampleType* src = static_cast<const SrcSampleType*>(src_void);

  for (uint32_t src_idx = next_src_idx_to_copy; src_idx < next_src_idx_to_copy + frames_needed;
       ++src_idx, ++next_cache_idx_to_fill) {
    auto src_frame = src + (src_idx * SrcChanCount);

    // Do this one dest_chan at a time
    for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
      (*channel_strip)[dest_chan][next_cache_idx_to_fill] = SR::Read(src_frame, dest_chan);
    }
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool SincSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src_void,
    uint32_t frac_src_frames, int32_t* frac_src_offset) {
  TRACE_DURATION("audio", "SincSamplerImpl::MixInternal");

  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto info = &bookkeeping();
  int32_t frac_src_off = *frac_src_offset;
  position_.SetSourceValues(src_void, frac_src_frames, frac_src_offset);
  position_.SetDestValues(dest, dest_frames, dest_offset);
  position_.SetRateValues(info->step_size, info->rate_modulo, info->denominator,
                          &info->src_pos_modulo);

  const uint32_t src_frames = frac_src_frames >> kPtsFractionalBits;
  uint32_t next_cache_idx_to_fill = 0;
  int32_t next_src_idx_to_copy =
      RightIdx(frac_src_off - static_cast<int32_t>(neg_filter_width().raw_value()));
  int32_t src_offset = frac_src_off >> kPtsFractionalBits;

  // Do we need previously-cached values?
  if (next_src_idx_to_copy < 0) {
    next_cache_idx_to_fill = 0 - next_src_idx_to_copy;
    next_src_idx_to_copy = 0;
  }

  // If we don't have enough source or dest to mix even one frame, get out. Before leaving, if we've
  // reached the end of the source buffer, then cache the last few source frames for the next mix.
  if (!position_.FrameCanBeMixed()) {
    if (position_.SourceIsConsumed()) {
      const auto frames_needed = src_frames - next_src_idx_to_copy;

      // Calculate/store the last few source frames to the start of the channel_strip for next time
      PopulateFramesToChannelStrip(src_void, next_src_idx_to_copy, frames_needed, &working_data_,
                                   next_cache_idx_to_fill);
      return true;
    }
    return false;
  }

  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    __UNUSED uint32_t dest_ramp_start;  // only used when ramping
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    } else {
      dest_ramp_start = position_.dest_offset();
    }

    while (position_.FrameCanBeMixed()) {
      auto src_offset_to_cache =
          RightIdx(frac_src_off - neg_filter_width().raw_value()) * Mixer::FRAC_ONE;
      const auto frames_needed = std::min<uint32_t>(src_frames - next_src_idx_to_copy,
                                                    kDataCacheLength - next_cache_idx_to_fill);

      // Bring in as much as a channel strip of source data (while channel/format-converting).
      PopulateFramesToChannelStrip(src_void, next_src_idx_to_copy, frames_needed, &working_data_,
                                   next_cache_idx_to_fill);
      next_src_idx_to_copy += frames_needed;

      uint32_t frac_cache_offset = frac_src_off - src_offset_to_cache;
      uint32_t interp_frac = frac_cache_offset & Mixer::FRAC_MASK;
      uint32_t cache_center_idx = LeftIdx(frac_cache_offset);
      FX_CHECK(RightIdx(frac_cache_offset - neg_filter_width().raw_value()) >= 0)
          << RightIdx(static_cast<int32_t>(cache_center_idx - neg_filter_width().raw_value()))
          << " should be >= 0";

      constexpr uint32_t kDataCacheFracLength = kDataCacheLength << kPtsFractionalBits;
      while (position_.FrameCanBeMixed() &&
             (frac_cache_offset + pos_filter_width().raw_value() < kDataCacheFracLength)) {
        auto dest_frame = position_.CurrentDestFrame();
        if constexpr (ScaleType == ScalerType::RAMPING) {
          amplitude_scale = info->scale_arr[position_.dest_offset() - dest_ramp_start];
        }

        for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
          float sample =
              filter_.ComputeSample(interp_frac, &(working_data_[dest_chan][cache_center_idx]));
          dest_frame[dest_chan] = DM::Mix(dest_frame[dest_chan], sample, amplitude_scale);
        }

        frac_src_off = position_.AdvanceFrame<HasModulo>();

        frac_cache_offset = frac_src_off - src_offset_to_cache;
        interp_frac = frac_cache_offset & Mixer::FRAC_MASK;
        cache_center_idx = LeftIdx(frac_cache_offset);
      }

      // idx of the earliest cached frame we must retain == the amount by which we can left-shift
      auto num_frames_to_shift = RightIdx(frac_cache_offset - neg_filter_width().raw_value());
      working_data_.ShiftBy(num_frames_to_shift);

      cache_center_idx -= num_frames_to_shift;
      src_offset += num_frames_to_shift;
      next_cache_idx_to_fill = kDataCacheLength - num_frames_to_shift;
    }
  } else {
    auto num_src_frames_skipped = position_.AdvanceToEnd<HasModulo>();
    working_data_.ShiftBy(num_src_frames_skipped);
  }

  position_.UpdateOffsets();
  return position_.SourceIsConsumed();
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool SincSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, bool accumulate) {
  auto info = &bookkeeping();
  bool hasModulo = (info->denominator > 0 && info->rate_modulo > 0);

  // For now, we continue to use this proliferation of template specializations, largely to keep the
  // designs congruent between the Point, Linear and Sinc samplers. Previously when we removed most
  // of these specializations from Point/Linear, we regained only about 75K of blob space, while
  // mixer microbenchmarks running times ballooned to ~3x of their previous durations.
  if (info->gain.IsUnity()) {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::EQ_UNITY, true, true>(dest, dest_frames, dest_offset, src,
                                                              frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::EQ_UNITY, true, false>(dest, dest_frames, dest_offset, src,
                                                               frac_src_frames, frac_src_offset))
               : (hasModulo
                      ? Mix<ScalerType::EQ_UNITY, false, true>(dest, dest_frames, dest_offset, src,
                                                               frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::EQ_UNITY, false, false>(dest, dest_frames, dest_offset, src,
                                                                frac_src_frames, frac_src_offset));
  } else if (info->gain.IsSilent()) {
    return (hasModulo ? Mix<ScalerType::MUTED, true, true>(dest, dest_frames, dest_offset, src,
                                                           frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::MUTED, true, false>(dest, dest_frames, dest_offset, src,
                                                            frac_src_frames, frac_src_offset));
  } else if (info->gain.IsRamping()) {
    const auto max_frames = Mixer::Bookkeeping::kScaleArrLen + *dest_offset;
    if (dest_frames > max_frames) {
      dest_frames = max_frames;
    }

    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::RAMPING, true, true>(dest, dest_frames, dest_offset, src,
                                                             frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::RAMPING, true, false>(dest, dest_frames, dest_offset, src,
                                                              frac_src_frames, frac_src_offset))
               : (hasModulo
                      ? Mix<ScalerType::RAMPING, false, true>(dest, dest_frames, dest_offset, src,
                                                              frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::RAMPING, false, false>(dest, dest_frames, dest_offset, src,
                                                               frac_src_frames, frac_src_offset));
  } else {
    return accumulate
               ? (hasModulo
                      ? Mix<ScalerType::NE_UNITY, true, true>(dest, dest_frames, dest_offset, src,
                                                              frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::NE_UNITY, true, false>(dest, dest_frames, dest_offset, src,
                                                               frac_src_frames, frac_src_offset))
               : (hasModulo
                      ? Mix<ScalerType::NE_UNITY, false, true>(dest, dest_frames, dest_offset, src,
                                                               frac_src_frames, frac_src_offset)
                      : Mix<ScalerType::NE_UNITY, false, false>(dest, dest_frames, dest_offset, src,
                                                                frac_src_frames, frac_src_offset));
  }
}

// Templates used to expand  the different combinations of possible SincSampler configurations.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  return std::make_unique<SincSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>>(
      src_format.frames_per_second, dest_format.frames_per_second);
}

template <size_t DestChanCount, typename SrcSampleType>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectSSM(dChan,sType)");

  switch (src_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return SelectSSM<DestChanCount, SrcSampleType, 1>(src_format, dest_format);
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return SelectSSM<DestChanCount, SrcSampleType, 2>(src_format, dest_format);
      }
      break;
    case 3:
      // Unlike other samplers, we handle 3:3 here since there is no NxN sinc sampler variant.
      if constexpr (DestChanCount <= 3) {
        return SelectSSM<DestChanCount, SrcSampleType, 3>(src_format, dest_format);
      }
      break;
    case 4:
      // Unlike other samplers, we handle 4:4 here since there is no NxN sinc sampler variant.
      if constexpr (DestChanCount <= 2 || DestChanCount == 4) {
        return SelectSSM<DestChanCount, SrcSampleType, 4>(src_format, dest_format);
      }
      break;
    default:
      break;
  }
  return nullptr;
}

template <size_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& src_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectSSM(dChan)");

  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectSSM<DestChanCount, uint8_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectSSM<DestChanCount, int16_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectSSM<DestChanCount, int32_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectSSM<DestChanCount, float>(src_format, dest_format);
    default:
      return nullptr;
  }
}

std::unique_ptr<Mixer> SincSampler::Select(const fuchsia::media::AudioStreamType& src_format,
                                           const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SincSampler::Select");

  if ((src_format.channels < 1 || dest_format.channels < 1) ||
      (src_format.channels > 4 || dest_format.channels > 4)) {
    return nullptr;
  }

  switch (dest_format.channels) {
    case 1:
      return SelectSSM<1>(src_format, dest_format);
    case 2:
      return SelectSSM<2>(src_format, dest_format);
    case 3:
      return SelectSSM<3>(src_format, dest_format);
    case 4:
      // For now, to mix Mono and Stereo sources to 4-channel destinations, we duplicate source
      // channels across multiple destinations (Stereo LR becomes LRLR, Mono M becomes MMMM).
      // Audio formats do not include info needed to filter frequencies or locate channels in 3D
      // space.
      // TODO(MTWN-399): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(MTWN-402): account for frequency range (e.g. a "4-channel" stereo woofer+tweeter).
      return SelectSSM<4>(src_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace media::audio::mixer
