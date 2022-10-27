// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sinc_sampler.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include <ffl/string.h>

#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "src/media/audio/lib/format2/channel_mapper.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/channel_strip.h"
#include "src/media/audio/lib/processing/filter.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/processing/position_manager.h"

namespace media_audio {

namespace {

template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class SincSamplerImpl : public SincSampler {
 public:
  SincSamplerImpl(int32_t source_frame_rate, int32_t dest_frame_rate)
      : SincSampler(SincFilter::Length(source_frame_rate, dest_frame_rate),
                    // Sinc filters are symmetric.
                    SincFilter::Length(source_frame_rate, dest_frame_rate)),
        source_frame_rate_(source_frame_rate),
        dest_frame_rate_(dest_frame_rate),
        position_(SourceChannelCount, DestChannelCount, pos_filter_length().raw_value(),
                  neg_filter_length().raw_value()),
        working_data_(DestChannelCount, kDataCacheLength),
        // `SincFilter` holds one side of coefficients (positive), we invert the position to
        // calculate the other side (negative).
        filter_(source_frame_rate_, dest_frame_rate_, pos_filter_length().raw_value()) {
    FX_CHECK(pos_filter_length() == neg_filter_length())
        << "SincSampler assumes a symmetric filter, pos_filter_length (" << ffl::String::DecRational
        << pos_filter_length() << ") != neg_filter_length (" << neg_filter_length() << ")";

    const int64_t cache_length_needed =
        Sampler::Floor(neg_filter_length().raw_value() + pos_filter_length().raw_value() - 1);
    FX_CHECK(kDataCacheLength >= cache_length_needed)
        << "Data cache (len " << kDataCacheLength << ") must be at least " << cache_length_needed
        << " long to support SRC ratio " << source_frame_rate << "/" << dest_frame_rate;
  }

  void EagerlyPrepare() final { filter_.EagerlyPrepare(); }

  void Process(Source source, Dest dest, Gain gain, bool accumulate) final {
    TRACE_DURATION("audio", "SincSamplerImpl::Process", "source_rate", source_frame_rate_,
                   "dest_rate", dest_frame_rate_, "source_chans", SourceChannelCount, "dest_chans",
                   DestChannelCount);

    PositionManager::CheckPositions(dest.frame_count, dest.frame_offset_ptr, source.frame_count,
                                    source.frame_offset_ptr->raw_value(),
                                    pos_filter_length().raw_value(),
                                    state().step_size().raw_value(), state().step_size_modulo(),
                                    state().step_size_denominator(), state().source_pos_modulo());
    position_.SetRateValues(state().step_size().raw_value(), state().step_size_modulo(),
                            state().step_size_denominator(), state().source_pos_modulo());
    position_.SetSourceValues(source.samples, source.frame_count, source.frame_offset_ptr);
    position_.SetDestValues(dest.samples, dest.frame_count, dest.frame_offset_ptr);

    switch (gain.type) {
      case GainType::kSilent:
        if (accumulate) {
          ProcessWith<GainType::kSilent, true>(source, dest, gain);
        } else {
          ProcessWith<GainType::kSilent, false>(source, dest, gain);
        }
        break;
      case GainType::kNonUnity:
        if (accumulate) {
          ProcessWith<GainType::kNonUnity, true>(source, dest, gain);
        } else {
          ProcessWith<GainType::kNonUnity, false>(source, dest, gain);
        }
        break;
      case GainType::kUnity:
        if (accumulate) {
          ProcessWith<GainType::kUnity, true>(source, dest, gain);
        } else {
          ProcessWith<GainType::kUnity, false>(source, dest, gain);
        }
        break;
      case GainType::kRamping:
        if (accumulate) {
          ProcessWith<GainType::kRamping, true>(source, dest, gain);
        } else {
          ProcessWith<GainType::kRamping, false>(source, dest, gain);
        }
        break;
      default:
        break;
    }

    if (state().step_size_modulo() > 0) {
      state().set_source_pos_modulo(position_.source_pos_modulo());
    }
  }

 private:
  static void PopulateFramesToChannelStrip(const void* source_void_ptr,
                                           int64_t next_source_idx_to_copy,
                                           const int64_t frames_needed, ChannelStrip* channel_strip,
                                           int64_t next_cache_idx_to_fill) {
    if constexpr (kTracePositionEvents) {
      TRACE_DURATION("audio", __func__, "next_source_idx_to_copy", next_source_idx_to_copy,
                     "frames_needed", frames_needed, "next_cache_idx_to_fill",
                     next_cache_idx_to_fill);
    }

    const SourceSampleType* source_ptr = static_cast<const SourceSampleType*>(source_void_ptr);
    for (int64_t source_idx = next_source_idx_to_copy;
         source_idx < next_source_idx_to_copy + frames_needed;
         ++source_idx, ++next_cache_idx_to_fill) {
      const SourceSampleType* source_frame = &source_ptr[source_idx * SourceChannelCount];
      for (size_t dest_chan = 0; dest_chan < DestChannelCount; ++dest_chan) {
        (*channel_strip)[dest_chan][next_cache_idx_to_fill] = mapper_.Map(source_frame, dest_chan);
      }
    }
  }

  // Processes `source` into `dest` with gain `Type`.
  template <GainType Type, bool Accumulate>
  void ProcessWith(const Source& source, const Dest& dest, const Gain& gain) {
    int64_t frac_source_offset = source.frame_offset_ptr->raw_value();
    const auto frac_filter_width = pos_filter_length().raw_value() - 1;

    int64_t next_cache_idx_to_fill = 0;
    auto next_source_idx_to_copy = Sampler::Ceiling(frac_source_offset - frac_filter_width);

    // Do we need previously-cached values?
    if (next_source_idx_to_copy < 0) {
      next_cache_idx_to_fill = -next_source_idx_to_copy;
      next_source_idx_to_copy = 0;
    }

    // If we don't have enough source or dest to mix even one frame, get out. Before leaving, if
    // we've reached the end of the source buffer, then cache the last few source frames for the
    // next mix.
    if (!position_.CanFrameBeMixed()) {
      if (position_.IsSourceConsumed()) {
        const auto frames_needed = source.frame_count - next_source_idx_to_copy;
        if (frac_source_offset > 0) {
          working_data_.ShiftBy(Sampler::Ceiling(frac_source_offset));
        }

        // Calculate and store the last few source frames to start of channel_strip, for next time.
        // If muted, this is unnecessary because we've already shifted in zeroes (silence).
        if constexpr (Type != GainType::kSilent) {
          PopulateFramesToChannelStrip(source.samples, next_source_idx_to_copy, frames_needed,
                                       &working_data_, next_cache_idx_to_fill);
        }
      }
      return;
    }

    if constexpr (Type != media_audio::GainType::kSilent) {
      auto frac_source_offset_to_cache =
          Sampler::Ceiling(frac_source_offset - frac_filter_width) * kFracOneFrame;
      auto frames_needed = std::min(source.frame_count - next_source_idx_to_copy,
                                    kDataCacheLength - next_cache_idx_to_fill);

      // Bring in as much as a channel strip of source data (while channel/format-converting).
      PopulateFramesToChannelStrip(source.samples, next_source_idx_to_copy, frames_needed,
                                   &working_data_, next_cache_idx_to_fill);

      float scale = gain.scale;
      int64_t dest_ramp_start = position_.dest_offset();  // only used when ramping
      while (position_.CanFrameBeMixed()) {
        next_source_idx_to_copy += frames_needed;

        int64_t frac_cache_offset = frac_source_offset - frac_source_offset_to_cache;
        int64_t frac_interp_fraction = frac_cache_offset & Fixed::Format::FractionalMask;
        auto cache_center_idx = Sampler::Floor(frac_cache_offset);
        FX_CHECK(Sampler::Ceiling(frac_cache_offset - frac_filter_width) >= 0)
            << Sampler::Ceiling(frac_cache_offset - frac_filter_width) << " should be >= 0";
        if constexpr (kTracePositionEvents) {
          TRACE_DURATION("audio", "SincSampler::Process chunk", "next_source_idx_to_copy",
                         next_source_idx_to_copy, "cache_center_idx", cache_center_idx);
        }

        while (position_.CanFrameBeMixed() &&
               frac_cache_offset + frac_filter_width < kFracDataCacheLength) {
          float* dest_frame = position_.CurrentDestFrame();
          if constexpr (Type == media_audio::GainType::kRamping) {
            scale = gain.scale_ramp[position_.dest_offset() - dest_ramp_start];
          }

          for (size_t dest_chan = 0; dest_chan < DestChannelCount; ++dest_chan) {
            const float sample = filter_.ComputeSample(
                frac_interp_fraction, &(working_data_[dest_chan][cache_center_idx]));
            MixSample<Type, Accumulate>(sample, &dest_frame[dest_chan], scale);
          }

          frac_source_offset = position_.AdvanceFrame();

          frac_cache_offset = frac_source_offset - frac_source_offset_to_cache;
          frac_interp_fraction = frac_cache_offset & Fixed::Format::FractionalMask;
          cache_center_idx = Sampler::Floor(frac_cache_offset);
        }

        // idx of the earliest cached frame we must retain == the amount by which we can left-shift.
        const auto num_frames_to_shift = Sampler::Ceiling(frac_cache_offset - frac_filter_width);
        working_data_.ShiftBy(num_frames_to_shift);

        cache_center_idx -= num_frames_to_shift;
        next_cache_idx_to_fill = kDataCacheLength - num_frames_to_shift;

        frac_source_offset_to_cache =
            Sampler::Ceiling(frac_source_offset - frac_filter_width) * kFracOneFrame;
        frames_needed = std::min(source.frame_count - next_source_idx_to_copy,
                                 kDataCacheLength - next_cache_idx_to_fill);

        PopulateFramesToChannelStrip(source.samples, next_source_idx_to_copy, frames_needed,
                                     &working_data_, next_cache_idx_to_fill);
      }
    } else {
      if constexpr (!Accumulate) {
        // Zero fill destination frames.
        const int64_t dest_frame_offset = *dest.frame_offset_ptr;
        std::fill_n(&dest.samples[dest_frame_offset * DestChannelCount],
                    (dest.frame_count - dest_frame_offset) * DestChannelCount, 0.0f);
      }
      auto num_source_frames_skipped = position_.AdvanceToEnd();
      working_data_.ShiftBy(num_source_frames_skipped);
    }

    position_.UpdateOffsets();
  }

  // Our `ChannelStrip` must fit even the widest filter.
  static constexpr int64_t kDataCacheLength = Sampler::Floor(
      SincFilter::kMaxFracSideLength + kFracOneFrame + SincFilter::kMaxFracSideLength);
  static constexpr int64_t kFracDataCacheLength = kDataCacheLength << Fixed::Format::FractionalBits;

  static inline ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount> mapper_;

  const int32_t source_frame_rate_;
  const int32_t dest_frame_rate_;

  PositionManager position_;
  ChannelStrip working_data_;
  SincFilter filter_;
};

// Helper functions to expand the combinations of possible `SincSamplerImpl` configurations.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
std::shared_ptr<Sampler> CreateWith(const Format& source_format, const Format& dest_format) {
  return std::make_shared<SincSamplerImpl<SourceSampleType, SourceChannelCount, DestChannelCount>>(
      static_cast<int32_t>(source_format.frames_per_second()),
      static_cast<int32_t>(dest_format.frames_per_second()));
}

template <typename SourceSampleType, size_t SourceChannelCount>
std::shared_ptr<Sampler> CreateWith(const Format& source_format, const Format& dest_format) {
  switch (dest_format.channels()) {
    case 1:
      return CreateWith<SourceSampleType, SourceChannelCount, 1>(source_format, dest_format);
    case 2:
      return CreateWith<SourceSampleType, SourceChannelCount, 2>(source_format, dest_format);
    case 3:
      if constexpr (SourceChannelCount <= 3) {
        return CreateWith<SourceSampleType, SourceChannelCount, 3>(source_format, dest_format);
      }
      break;
    case 4:
      if constexpr (SourceChannelCount != 3) {
        return CreateWith<SourceSampleType, SourceChannelCount, 4>(source_format, dest_format);
      }
      break;
    default:
      break;
  }
  FX_LOGS(WARNING) << "SincSampler does not support this channelization: " << SourceChannelCount
                   << " -> " << dest_format.channels();
  return nullptr;
}

template <typename SourceSampleType>
std::shared_ptr<Sampler> CreateWith(const Format& source_format, const Format& dest_format) {
  switch (source_format.channels()) {
    case 1:
      return CreateWith<SourceSampleType, 1>(source_format, dest_format);
    case 2:
      return CreateWith<SourceSampleType, 2>(source_format, dest_format);
    case 3:
      return CreateWith<SourceSampleType, 3>(source_format, dest_format);
    case 4:
      return CreateWith<SourceSampleType, 4>(source_format, dest_format);
    default:
      FX_LOGS(WARNING) << "SincSampler does not support this channelization: "
                       << source_format.channels() << " -> " << dest_format.channels();
      return nullptr;
  }
}

}  // namespace

std::shared_ptr<Sampler> SincSampler::Create(const Format& source_format,
                                             const Format& dest_format) {
  TRACE_DURATION("audio", "SincSampler::Create");

  if (dest_format.sample_type() != fuchsia_audio::SampleType::kFloat32) {
    FX_LOGS(WARNING) << "SincSampler does not support this dest sample type: "
                     << static_cast<uint32_t>(dest_format.sample_type());
    return nullptr;
  }

  switch (source_format.sample_type()) {
    case fuchsia_audio::SampleType::kUint8:
      return CreateWith<uint8_t>(source_format, dest_format);
    case fuchsia_audio::SampleType::kInt16:
      return CreateWith<int16_t>(source_format, dest_format);
    case fuchsia_audio::SampleType::kInt32:
      return CreateWith<int32_t>(source_format, dest_format);
    case fuchsia_audio::SampleType::kFloat32:
      return CreateWith<float>(source_format, dest_format);
    default:
      // TODO(fxbug.dev/87651): support float64?
      FX_LOGS(WARNING) << "SincSampler does not support this source sample type: "
                       << static_cast<uint32_t>(source_format.sample_type());
      return nullptr;
  }
}

}  // namespace media_audio
