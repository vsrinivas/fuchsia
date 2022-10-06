// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/point_sampler.h"

#include <fidl/fuchsia.audio/cpp/natural_ostream.h>
#include <fidl/fuchsia.audio/cpp/wire_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "src/media/audio/lib/format2/channel_mapper.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/processing/position_manager.h"

namespace media_audio {

namespace {

// `PointSampler` is only used for 1:1 frame rate conversions. In such unity conversion cases,
// there may be situations where samples would continuously arrive at exactly halfway between two
// source frames. To resolve these into integral destination frames without introducing any
// latency, by preserving zero-phase, we would have to continuously average those two neighbouring
// source frames. However, this could potentially lead to a reduced output response at higher
// frequencies in a typical implementation, since we would compute each output frame by a linear
// interpolation of those two neighbouring frames. To avoid this issue, we always snap to the
// forward nearest neighbor sample directly without interpolation, i.e. choosing the newer frame
// position when the fractional sampling position is exactly in the middle between two positions.
constexpr int64_t kFracPositiveFilterLength = kFracHalfFrame + 1;
constexpr int64_t kFracNegativeFilterLength = kFracHalfFrame;

template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl()
      : PointSampler(Fixed::FromRaw(kFracPositiveFilterLength),
                     Fixed::FromRaw(kFracNegativeFilterLength)) {}

  void EagerlyPrepare() final {}

  void Process(Source source, Dest dest, Gain gain, bool accumulate) final {
    TRACE_DURATION("audio", "PointSampler::Process");

    PositionManager::CheckPositions(dest.frame_count, dest.frame_offset_ptr, source.frame_count,
                                    source.frame_offset_ptr->raw_value(),
                                    pos_filter_length().raw_value(),
                                    state().step_size().raw_value(), state().step_size_modulo(),
                                    state().step_size_denominator(), state().source_pos_modulo());

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
  }

 private:
  // Processes `source` into `dest` with gain `Type`.
  template <GainType Type, bool Accumulate>
  static void ProcessWith(const Source& source, const Dest& dest, const Gain& gain) {
    auto& dest_frame_offset = *dest.frame_offset_ptr;
    if (dest_frame_offset >= dest.frame_count) {
      // Nothing to process.
      return;
    }

    // `source_frac_end` is the first subframe for which this call cannot produce output, since
    // processing output centered on this position (or beyond) requires data that we don't have yet.
    auto& source_frame_offset = *source.frame_offset_ptr;
    const auto source_frac_offset = source_frame_offset.raw_value();
    const int64_t source_frac_end =
        (source.frame_count << Fixed::Format::FractionalBits) - kFracPositiveFilterLength + 1;
    if (source_frac_offset >= source_frac_end) {
      return;
    }

    // Process destination frames.
    const int64_t frames_to_process =
        std::min(Sampler::Ceiling(source_frac_end - source_frac_offset),
                 dest.frame_count - dest_frame_offset);

    if constexpr (Type != GainType::kSilent) {
      const auto* source_frame = &static_cast<const SourceSampleType*>(
          source.samples)[Sampler::Floor(source_frac_offset + kFracPositiveFilterLength - 1) *
                          SourceChannelCount];
      float* dest_frame = &dest.samples[dest_frame_offset * DestChannelCount];

      float scale = gain.scale;
      for (int64_t frame = 0; frame < frames_to_process; ++frame) {
        if constexpr (Type == GainType::kRamping) {
          scale = gain.scale_ramp[frame];
        }
        for (size_t dest_channel = 0; dest_channel < DestChannelCount; ++dest_channel) {
          MixSample<Type, Accumulate>(mapper_.Map(source_frame, dest_channel),
                                      &dest_frame[dest_channel], scale);
        }
        source_frame += SourceChannelCount;
        dest_frame += DestChannelCount;
      }
    } else if constexpr (!Accumulate) {
      // Zero fill destination frames.
      std::fill_n(&dest.samples[dest_frame_offset * DestChannelCount],
                  frames_to_process * DestChannelCount, 0.0f);
    }

    // Advance the source and destination frame offsets by `frames_to_process`.
    source_frame_offset =
        Fixed::FromRaw(source_frac_offset + (frames_to_process << Fixed::Format::FractionalBits));
    dest_frame_offset += frames_to_process;
  }

  static inline ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount> mapper_;
};

// Helper functions to expand the combinations of possible `PointSamplerImpl` configurations.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
std::shared_ptr<Sampler> CreateWith() {
  return std::make_shared<
      PointSamplerImpl<SourceSampleType, SourceChannelCount, DestChannelCount>>();
}

template <typename SourceSampleType, size_t SourceChannelCount>
std::shared_ptr<Sampler> CreateWith(int64_t dest_channel_count) {
  switch (dest_channel_count) {
    case 1:
      return CreateWith<SourceSampleType, SourceChannelCount, 1>();
    case 2:
      return CreateWith<SourceSampleType, SourceChannelCount, 2>();
    case 3:
      if constexpr (SourceChannelCount <= 3) {
        return CreateWith<SourceSampleType, SourceChannelCount, 3>();
      }
      break;
    case 4:
      if constexpr (SourceChannelCount != 3) {
        return CreateWith<SourceSampleType, SourceChannelCount, 4>();
      }
      break;
    default:
      break;
  }
  FX_LOGS(WARNING) << "PointSampler does not support this channelization: " << SourceChannelCount
                   << " -> " << dest_channel_count;
  return nullptr;
}

template <typename SourceSampleType>
std::shared_ptr<Sampler> CreateWith(int64_t source_channel_count, int64_t dest_channel_count) {
  // N -> N channel configuration.
  if (source_channel_count == dest_channel_count) {
    switch (source_channel_count) {
      case 1:
        return CreateWith<SourceSampleType, 1, 1>();
      case 2:
        return CreateWith<SourceSampleType, 2, 2>();
      case 3:
        return CreateWith<SourceSampleType, 3, 3>();
      case 4:
        return CreateWith<SourceSampleType, 4, 4>();
      case 5:
        return CreateWith<SourceSampleType, 5, 5>();
      case 6:
        return CreateWith<SourceSampleType, 6, 6>();
      case 7:
        return CreateWith<SourceSampleType, 7, 7>();
      case 8:
        return CreateWith<SourceSampleType, 8, 8>();
      default:
        FX_LOGS(WARNING) << "PointSampler does not support this channelization: "
                         << source_channel_count << " -> " << dest_channel_count;
        return nullptr;
    }
  }

  // M -> N channel configuration.
  switch (source_channel_count) {
    case 1:
      return CreateWith<SourceSampleType, 1>(dest_channel_count);
    case 2:
      return CreateWith<SourceSampleType, 2>(dest_channel_count);
    case 3:
      return CreateWith<SourceSampleType, 3>(dest_channel_count);
    case 4:
      return CreateWith<SourceSampleType, 4>(dest_channel_count);
    default:
      FX_LOGS(WARNING) << "PointSampler does not support this channelization: "
                       << source_channel_count << " -> " << dest_channel_count;
      return nullptr;
  }
}

}  // namespace

std::shared_ptr<Sampler> PointSampler::Create(const Format& source_format,
                                              const Format& dest_format) {
  TRACE_DURATION("audio", "PointSampler::Create");

  if (source_format.frames_per_second() != dest_format.frames_per_second()) {
    FX_LOGS(WARNING) << "PointSampler source frame rate " << source_format.frames_per_second()
                     << " must be equal to dest frame rate " << dest_format.frames_per_second();
    return nullptr;
  }

  if (dest_format.sample_type() != fuchsia_audio::SampleType::kFloat32) {
    FX_LOGS(WARNING) << "PointSampler does not support this dest sample type: "
                     << dest_format.sample_type();
    return nullptr;
  }

  const int64_t source_channel_count = source_format.channels();
  const int64_t dest_channel_count = dest_format.channels();
  switch (source_format.sample_type()) {
    case fuchsia_audio::SampleType::kUint8:
      return CreateWith<uint8_t>(source_channel_count, dest_channel_count);
    case fuchsia_audio::SampleType::kInt16:
      return CreateWith<int16_t>(source_channel_count, dest_channel_count);
    case fuchsia_audio::SampleType::kInt32:
      return CreateWith<int32_t>(source_channel_count, dest_channel_count);
    case fuchsia_audio::SampleType::kFloat32:
      return CreateWith<float>(source_channel_count, dest_channel_count);
    default:
      // TODO(fxbug.dev/87651): support float64?
      FX_LOGS(WARNING) << "PointSampler does not support this source sample type: "
                       << static_cast<uint32_t>(source_format.sample_type());
      return nullptr;
  }
}

}  // namespace media_audio
