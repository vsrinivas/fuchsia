// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/sinc_sampler.h"

#include <lib/trace/event.h>

#include <algorithm>
#include <limits>

#include "lib/syslog/cpp/macros.h"
#include "src/media/audio/audio_core/mixer/channel_strip.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/filter.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"
#include "src/media/audio/audio_core/mixer/position_manager.h"

namespace media::audio::mixer {

// Note that this value directly determines the maximum downsampling ratio: the ratio's numerator is
// (kDataCacheLength/28). For example, if kDataCacheLength is 280, max downsampling ratio is 10:1.
//
// 'audio_fidelity_tests --profile' measured the performance of various lengths. Length 680 had
// better performance than other measured lengths (280, 560, 640, 700, 720, 1000, 1344), presumably
// because of cache/locality effects. This length allows a downsampling ratio greater than 24:1 --
// even with 192kHz input hardware, we can produce 8kHz streams to capturers.
static constexpr int64_t kDataCacheLength = 680;
static constexpr int64_t kDataCacheFracLength = kDataCacheLength << Fixed::Format::FractionalBits;

template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
class SincSamplerImpl : public SincSampler {
 public:
  SincSamplerImpl(uint32_t source_frame_rate, uint32_t dest_frame_rate)
      : SincSampler(SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate),
                    SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate)),
        source_rate_(source_frame_rate),
        dest_rate_(dest_frame_rate),
        position_(SourceChanCount, DestChanCount,
                  SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate).raw_value(),
                  SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate).raw_value()),
        working_data_(DestChanCount, kDataCacheLength),
        filter_(source_rate_, dest_rate_,
                SincFilter::GetFilterWidth(source_frame_rate, dest_frame_rate).raw_value()) {
    num_prev_frames_needed_ = Ceiling(neg_filter_width().raw_value());
    total_frames_needed_ = num_prev_frames_needed_ + Ceiling(pos_filter_width().raw_value());

    FX_CHECK(kDataCacheLength > total_frames_needed_)
        << "Data cache (len " << kDataCacheLength << ") must be at least " << total_frames_needed_
        << " to support SRC ratio " << source_frame_rate << "/" << dest_frame_rate;
  }

  bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
           const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr,
           bool accumulate) override;

  void Reset() override {
    SincSampler::Reset();
    working_data_.Clear();
  }

  virtual void EagerlyPrepare() override { filter_.EagerlyPrepare(); }

 private:
  // As an optimization, we work with raw fixed-point values internally, but we pass Fixed types
  // through our public interfaces (to MixStage etc.) for source position/filter width/step size.
  static constexpr int64_t kFracFrame = kOneFrame.raw_value();

  static int64_t Ceiling(int64_t frac_position) {
    return ((frac_position - 1) >> Fixed::Format::FractionalBits) + 1;
  }
  static int64_t Floor(int64_t frac_position) {
    return frac_position >> Fixed::Format::FractionalBits;
  }

  template <ScalerType ScaleType, bool DoAccumulate>
  inline bool Mix(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr,
                  const void* source_void_ptr, int64_t source_frames, Fixed* source_offset_ptr);

  static inline void PopulateFramesToChannelStrip(const void* source_void_ptr,
                                                  int64_t next_source_idx_to_copy,
                                                  const int64_t frames_needed,
                                                  ChannelStrip* channel_strip,
                                                  int64_t next_cache_idx_to_fill);

  uint32_t source_rate_;
  uint32_t dest_rate_;
  int64_t num_prev_frames_needed_;
  int64_t total_frames_needed_;

  PositionManager position_;
  ChannelStrip working_data_;
  SincFilter filter_;
};

// Implementation
//
// static
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
inline void
SincSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::PopulateFramesToChannelStrip(
    const void* source_void_ptr, int64_t next_source_idx_to_copy, const int64_t frames_needed,
    ChannelStrip* channel_strip, int64_t next_cache_idx_to_fill) {
  using SR = SourceReader<SourceSampleType, SourceChanCount, DestChanCount>;

  const SourceSampleType* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);

  for (int64_t source_idx = next_source_idx_to_copy;
       source_idx < next_source_idx_to_copy + frames_needed;
       ++source_idx, ++next_cache_idx_to_fill) {
    auto current_source_ptr = source_ptr + (source_idx * SourceChanCount);

    // Do this one dest_chan at a time
    for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
      (*channel_strip)[dest_chan][next_cache_idx_to_fill] = SR::Read(current_source_ptr, dest_chan);
    }
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE. They guarantee new
// buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool SincSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr, const void* source_void_ptr,
    int64_t source_frames, Fixed* source_offset_ptr) {
  TRACE_DURATION("audio", "SincSamplerImpl::MixInternal", "source_rate", source_rate_, "dest_rate",
                 dest_rate_, "source_chans", SourceChanCount, "dest_chans", DestChanCount);

  static_assert(ScaleType != ScalerType::MUTED || DoAccumulate == true,
                "Mixing muted streams without accumulation is explicitly unsupported");

  using DM = DestMixer<ScaleType, DoAccumulate>;

  auto info = &bookkeeping();
  auto frac_source_offset = source_offset_ptr->raw_value();
  const auto frac_neg_width = neg_filter_width().raw_value();
  position_.SetSourceValues(source_void_ptr, source_frames, source_offset_ptr);
  position_.SetDestValues(dest_ptr, dest_frames, dest_offset_ptr);
  position_.SetRateValues(info->step_size.raw_value(), info->rate_modulo(), info->denominator(),
                          &info->source_pos_modulo);

  int64_t next_cache_idx_to_fill = 0;
  auto next_source_idx_to_copy = Ceiling(frac_source_offset - frac_neg_width);

  // Do we need previously-cached values?
  if (next_source_idx_to_copy < 0) {
    next_cache_idx_to_fill = -next_source_idx_to_copy;
    next_source_idx_to_copy = 0;
  }

  // If we don't have enough source or dest to mix even one frame, get out. Before leaving, if we've
  // reached the end of the source buffer, then cache the last few source frames for the next mix.
  if (!position_.FrameCanBeMixed()) {
    if (position_.SourceIsConsumed()) {
      const auto frames_needed = source_frames - next_source_idx_to_copy;
      if (frac_source_offset > 0) {
        working_data_.ShiftBy(Ceiling(frac_source_offset));
      }

      // Calculate/store the last few source frames to the start of the channel_strip for next time
      PopulateFramesToChannelStrip(source_void_ptr, next_source_idx_to_copy, frames_needed,
                                   &working_data_, next_cache_idx_to_fill);
      return true;
    }
    return false;
  }

  if constexpr (ScaleType != ScalerType::MUTED) {
    Gain::AScale amplitude_scale;
    __UNUSED int64_t dest_ramp_start;  // only used when ramping
    if constexpr (ScaleType != ScalerType::RAMPING) {
      amplitude_scale = info->gain.GetGainScale();
    } else {
      dest_ramp_start = position_.dest_offset();
    }

    while (position_.FrameCanBeMixed()) {
      // Can't left-shift the potentially-negative result of Ceiling (undefined behavior)
      auto frac_source_offset_to_cache = Ceiling(frac_source_offset - frac_neg_width) * kFracFrame;
      const auto frames_needed = std::min(source_frames - next_source_idx_to_copy,
                                          kDataCacheLength - next_cache_idx_to_fill);

      // Bring in as much as a channel strip of source data (while channel/format-converting).
      PopulateFramesToChannelStrip(source_void_ptr, next_source_idx_to_copy, frames_needed,
                                   &working_data_, next_cache_idx_to_fill);
      next_source_idx_to_copy += frames_needed;

      int64_t frac_cache_offset = frac_source_offset - frac_source_offset_to_cache;
      int64_t frac_interp_fraction = frac_cache_offset & Fixed::Format::FractionalMask;
      auto cache_center_idx = Floor(frac_cache_offset);
      FX_CHECK(Ceiling(frac_cache_offset - frac_neg_width) >= 0)
          << Ceiling(frac_cache_offset - frac_neg_width) << " should be >= 0";

      while (position_.FrameCanBeMixed() &&
             frac_cache_offset + pos_filter_width().raw_value() < kDataCacheFracLength) {
        auto dest_frame = position_.CurrentDestFrame();
        if constexpr (ScaleType == ScalerType::RAMPING) {
          amplitude_scale = info->scale_arr[position_.dest_offset() - dest_ramp_start];
        }

        for (size_t dest_chan = 0; dest_chan < DestChanCount; ++dest_chan) {
          float sample = filter_.ComputeSample(frac_interp_fraction,
                                               &(working_data_[dest_chan][cache_center_idx]));
          dest_frame[dest_chan] = DM::Mix(dest_frame[dest_chan], sample, amplitude_scale);
        }

        frac_source_offset = position_.AdvanceFrame();

        frac_cache_offset = frac_source_offset - frac_source_offset_to_cache;
        frac_interp_fraction = frac_cache_offset & Fixed::Format::FractionalMask;
        cache_center_idx = Floor(frac_cache_offset);
      }

      // idx of the earliest cached frame we must retain == the amount by which we can left-shift
      auto num_frames_to_shift = Ceiling(frac_cache_offset - frac_neg_width);
      working_data_.ShiftBy(num_frames_to_shift);

      cache_center_idx -= num_frames_to_shift;
      next_cache_idx_to_fill = kDataCacheLength - num_frames_to_shift;
    }
  } else {
    auto num_source_frames_skipped = position_.AdvanceToEnd();
    working_data_.ShiftBy(num_source_frames_skipped);
  }

  position_.UpdateOffsets();
  return position_.SourceIsConsumed();
}

template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
bool SincSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>::Mix(
    float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr, const void* source_void_ptr,
    int64_t source_frames, Fixed* source_offset_ptr, bool accumulate) {
  auto info = &bookkeeping();

  PositionManager::CheckPositions(dest_frames, dest_offset_ptr, source_frames,
                                  source_offset_ptr->raw_value(), pos_filter_width().raw_value(),
                                  info);

  // For now, we continue to use this proliferation of template specializations, largely to keep the
  // designs congruent between the Point and Sinc samplers. Previously when we removed most
  // of these specializations from Point, we regained only about 75K of blob space, while
  // mixer microbenchmarks running times ballooned to ~3x of their previous durations.
  if (info->gain.IsUnity()) {
    return accumulate
               ? Mix<ScalerType::EQ_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                 source_void_ptr, source_frames, source_offset_ptr)
               : Mix<ScalerType::EQ_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                  source_void_ptr, source_frames,
                                                  source_offset_ptr);
  }

  if (info->gain.IsSilent()) {
    return Mix<ScalerType::MUTED, true>(dest_ptr, dest_frames, dest_offset_ptr, source_void_ptr,
                                        source_frames, source_offset_ptr);
  }

  if (info->gain.IsRamping()) {
    const auto max_frames = Mixer::Bookkeeping::kScaleArrLen + *dest_offset_ptr;
    if (dest_frames > max_frames) {
      dest_frames = max_frames;
    }

    return accumulate
               ? Mix<ScalerType::RAMPING, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                                source_void_ptr, source_frames, source_offset_ptr)
               : Mix<ScalerType::RAMPING, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                 source_void_ptr, source_frames, source_offset_ptr);
  }

  return accumulate
             ? Mix<ScalerType::NE_UNITY, true>(dest_ptr, dest_frames, dest_offset_ptr,
                                               source_void_ptr, source_frames, source_offset_ptr)
             : Mix<ScalerType::NE_UNITY, false>(dest_ptr, dest_frames, dest_offset_ptr,
                                                source_void_ptr, source_frames, source_offset_ptr);
}

// Templates used to expand  the different combinations of possible SincSampler configurations.
template <size_t DestChanCount, typename SourceSampleType, size_t SourceChanCount>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  return std::make_unique<SincSamplerImpl<DestChanCount, SourceSampleType, SourceChanCount>>(
      source_format.frames_per_second, dest_format.frames_per_second);
}

template <size_t DestChanCount, typename SourceSampleType>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectSSM(dChan,sType)");

  switch (source_format.channels) {
    case 1:
      if constexpr (DestChanCount <= 4) {
        return SelectSSM<DestChanCount, SourceSampleType, 1>(source_format, dest_format);
      }
      break;
    case 2:
      if constexpr (DestChanCount <= 4) {
        return SelectSSM<DestChanCount, SourceSampleType, 2>(source_format, dest_format);
      }
      break;
    case 3:
      // Unlike other samplers, we handle 3:3 here since there is no NxN sinc sampler variant.
      if constexpr (DestChanCount <= 3) {
        return SelectSSM<DestChanCount, SourceSampleType, 3>(source_format, dest_format);
      }
      break;
    case 4:
      // Unlike other samplers, we handle 4:4 here since there is no NxN sinc sampler variant.
      // Like other samplers, to mix 4-channel sources to Mono or Stereo destinations, we mix
      // (average) multiple source channels to each destination channel. Given a 4-channel source
      // (call it A|B|C|D), the Stereo output channel-mix would be [avg(A,C), avg(B,D)] and the Mono
      // output channel-mix would be [avg(A,B,C,D)].
      //
      // Audio formats do not include info needed to filter frequencies or 3D-locate channels.
      // TODO(fxbug.dev/13679): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(fxbug.dev/13682): account for frequency range (e.g. "4-channel" stereo woofer+tweeter)
      if constexpr (DestChanCount <= 2 || DestChanCount == 4) {
        return SelectSSM<DestChanCount, SourceSampleType, 4>(source_format, dest_format);
      }
      break;
    default:
      break;
  }
  FX_LOGS(WARNING) << "SincSampler does not support this channelization: " << source_format.channels
                   << " -> " << dest_format.channels;
  return nullptr;
}

template <size_t DestChanCount>
static inline std::unique_ptr<Mixer> SelectSSM(const fuchsia::media::AudioStreamType& source_format,
                                               const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SelectSSM(dChan)");

  switch (source_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectSSM<DestChanCount, uint8_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectSSM<DestChanCount, int16_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectSSM<DestChanCount, int32_t>(source_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectSSM<DestChanCount, float>(source_format, dest_format);
    default:
      FX_LOGS(WARNING) << "SincSampler does not support this sample_format: "
                       << static_cast<int64_t>(source_format.sample_format);
      return nullptr;
  }
}

std::unique_ptr<Mixer> SincSampler::Select(const fuchsia::media::AudioStreamType& source_format,
                                           const fuchsia::media::AudioStreamType& dest_format) {
  TRACE_DURATION("audio", "SincSampler::Select");

  if (source_format.channels > 4) {
    FX_LOGS(WARNING) << "SincSampler does not support this channelization: "
                     << source_format.channels << " -> " << dest_format.channels;
    return nullptr;
  }

  switch (dest_format.channels) {
    case 1:
      return SelectSSM<1>(source_format, dest_format);
    case 2:
      return SelectSSM<2>(source_format, dest_format);
    case 3:
      return SelectSSM<3>(source_format, dest_format);
    case 4:
      // For now, to mix Mono and Stereo sources to 4-channel destinations, we duplicate source
      // channels across multiple destinations (Stereo LR becomes LRLR, Mono M becomes MMMM).
      // Audio formats do not include info needed to filter frequencies or 3D-locate channels.
      // TODO(fxbug.dev/13679): enable the mixer to rechannelize in a more sophisticated way.
      // TODO(fxbug.dev/13682): account for frequency range (e.g. a "4-channel" stereo
      // woofer+tweeter).
      return SelectSSM<4>(source_format, dest_format);
    default:
      FX_LOGS(WARNING) << "SincSampler does not support this channelization: "
                       << source_format.channels << " -> " << dest_format.channels;
      return nullptr;
  }
}

}  // namespace media::audio::mixer
