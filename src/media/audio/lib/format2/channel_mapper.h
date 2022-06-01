// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_2_CHANNEL_MAPPER_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_2_CHANNEL_MAPPER_H_

#include <array>
#include <cmath>
#include <type_traits>
#include <utility>

#include "src/media/audio/lib/format2/sample_converter.h"

namespace media_audio {

// TODO(fxbug.dev/85201): Remove this workaround, once the device properly maps channels.
inline constexpr bool kEnable4ChannelWorkaround = true;

// Template to map a source frame of `SourceSampleType` with `SourceChannelCount` into each
// destination sample with `DestChannelCount` in a normalized 32-bit float format.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount,
          bool Customizable = false, typename Trait = void>
class ChannelMapper;

// N -> N channel mapper (passthrough).
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == DestChannelCount)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    return SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel]);
  }
};

// 1 -> N channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 1 && DestChannelCount > 1)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, [[maybe_unused]] size_t dest_channel) {
    return SampleConverter<SourceSampleType>::ToFloat(source_frame[0]);
  }
};

// 2 -> 1 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 2 && DestChannelCount == 1)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, [[maybe_unused]] size_t dest_channel) {
    // Assumes a configuration with equal weighting of each channel.
    return 0.5f * (SampleConverter<SourceSampleType>::ToFloat(source_frame[0]) +
                   SampleConverter<SourceSampleType>::ToFloat(source_frame[1]));
  }
};

// 2 -> 3 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 2 && DestChannelCount == 3)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    // Assumes a configuration where the third channel is an equally weighted downmix of the first
    // two channels.
    return (dest_channel < 2)
               ? SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel])
               : 0.5f * (SampleConverter<SourceSampleType>::ToFloat(source_frame[0]) +
                         SampleConverter<SourceSampleType>::ToFloat(source_frame[1]));
  }
};

// 2 -> 4 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 2 && DestChannelCount == 4)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    return SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel % 2]);
  }
};

// 3 -> 1 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 3 && DestChannelCount == 1)>> {
 public:
  // Assumes a configuration with equal weighting of each channel.
  inline float Map(const SourceSampleType* source_frame, [[maybe_unused]] size_t dest_channel) {
    return (SampleConverter<SourceSampleType>::ToFloat(source_frame[0]) +
            SampleConverter<SourceSampleType>::ToFloat(source_frame[1]) +
            SampleConverter<SourceSampleType>::ToFloat(source_frame[2])) /
           3.0f;
  }
};

// 3 -> 2 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 3 && DestChannelCount == 2)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    return SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel]) *
               kInverseOnePlusRootHalf +
           SampleConverter<SourceSampleType>::ToFloat(source_frame[2]) * kInverseRootTwoPlusOne;
  }

 private:
  static constexpr float kInverseOnePlusRootHalf = static_cast<float>(1.0 / (M_SQRT1_2 + 1.0));
  static constexpr float kInverseRootTwoPlusOne = static_cast<float>(1.0 / (M_SQRT2 + 1.0));
};

// 4 -> 1 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 4 && DestChannelCount == 1)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, [[maybe_unused]] size_t dest_channel) {
    if constexpr (kEnable4ChannelWorkaround) {
      // TODO(fxbug.dev/85201): Temporarily ignore the third and fourth channels.
      return 0.5f * (SampleConverter<SourceSampleType>::ToFloat(source_frame[0]) +
                     SampleConverter<SourceSampleType>::ToFloat(source_frame[1]));
    }
    return 0.25f * (SampleConverter<SourceSampleType>::ToFloat(source_frame[0]) +
                    SampleConverter<SourceSampleType>::ToFloat(source_frame[1]) +
                    SampleConverter<SourceSampleType>::ToFloat(source_frame[2]) +
                    SampleConverter<SourceSampleType>::ToFloat(source_frame[3]));
  }
};

// 4 -> 2 channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(SourceChannelCount == 4 && DestChannelCount == 2)>> {
 public:
  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    if constexpr (kEnable4ChannelWorkaround) {
      // TODO(fxbug.dev/85201): Temporarily ignore the third and fourth channels.
      return SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel]);
    }
    return 0.5f * (SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel]) +
                   SampleConverter<SourceSampleType>::ToFloat(source_frame[dest_channel + 2]));
  }
};

// M -> N customizable channel mapper.
template <typename SourceSampleType, size_t SourceChannelCount, size_t DestChannelCount>
class ChannelMapper<SourceSampleType, SourceChannelCount, DestChannelCount, /*Customizable=*/true> {
 public:
  explicit ChannelMapper(
      std::array<std::array<float, SourceChannelCount>, DestChannelCount> coefficients)
      : coefficients_(std::move(coefficients)) {}

  inline float Map(const SourceSampleType* source_frame, size_t dest_channel) {
    float dest_sample = 0.0f;
    for (size_t source_channel = 0; source_channel < SourceChannelCount; ++source_channel) {
      dest_sample += coefficients_[dest_channel][source_channel] *
                     SampleConverter<SourceSampleType>::ToFloat(source_frame[source_channel]);
    }
    return dest_sample;
  }

 private:
  // Normalized channel coefficients.
  std::array<std::array<float, SourceChannelCount>, DestChannelCount> coefficients_;
};

}  // namespace media_audio

#endif  //  SRC_MEDIA_AUDIO_LIB_FORMAT_2_CHANNEL_MAPPER_H_
