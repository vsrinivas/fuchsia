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

// Template to map an input frame of `InputSampleType` with `InputChannelCount` into each output
// sample with `OutputChannelCount` in a normalized 32-bit float format.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount,
          bool Customizable = false, typename Trait = void>
class ChannelMapper;

// N -> N channel mapper (passthrough).
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(InputChannelCount == OutputChannelCount)>> {
 public:
  inline float Map(const InputSampleType* input, size_t output_channel) {
    return SampleConverter<InputSampleType>::ToFloat(input[output_channel]);
  }
};

// 1 -> N channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
                    typename std::enable_if_t<(InputChannelCount == 1 && OutputChannelCount > 1)>> {
 public:
  inline float Map(const InputSampleType* input, [[maybe_unused]] size_t output_channel) {
    return SampleConverter<InputSampleType>::ToFloat(input[0]);
  }
};

// 2 -> 1 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 2 && OutputChannelCount == 1)>> {
 public:
  inline float Map(const InputSampleType* input, [[maybe_unused]] size_t output_channel) {
    // Assumes a configuration with equal weighting of each channel.
    return 0.5f * (SampleConverter<InputSampleType>::ToFloat(input[0]) +
                   SampleConverter<InputSampleType>::ToFloat(input[1]));
  }
};

// 2 -> 3 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 2 && OutputChannelCount == 3)>> {
 public:
  inline float Map(const InputSampleType* input, size_t output_channel) {
    // Assumes a configuration where the third channel is an equally weighted downmix of the first
    // two channels.
    return (output_channel < 2) ? SampleConverter<InputSampleType>::ToFloat(input[output_channel])
                                : 0.5f * (SampleConverter<InputSampleType>::ToFloat(input[0]) +
                                          SampleConverter<InputSampleType>::ToFloat(input[1]));
  }
};

// 2 -> 4 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 2 && OutputChannelCount == 4)>> {
 public:
  inline float Map(const InputSampleType* input, size_t output_channel) {
    return SampleConverter<InputSampleType>::ToFloat(input[output_channel % 2]);
  }
};

// 3 -> 1 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 3 && OutputChannelCount == 1)>> {
 public:
  // Assumes a configuration with equal weighting of each channel.
  inline float Map(const InputSampleType* input, [[maybe_unused]] size_t output_channel) {
    return (SampleConverter<InputSampleType>::ToFloat(input[0]) +
            SampleConverter<InputSampleType>::ToFloat(input[1]) +
            SampleConverter<InputSampleType>::ToFloat(input[2])) /
           3.0f;
  }
};

// 3 -> 2 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 3 && OutputChannelCount == 2)>> {
 public:
  inline float Map(const InputSampleType* input, size_t output_channel) {
    return SampleConverter<InputSampleType>::ToFloat(input[output_channel]) *
               kInverseOnePlusRootHalf +
           SampleConverter<InputSampleType>::ToFloat(input[2]) * kInverseRootTwoPlusOne;
  }

 private:
  static constexpr float kInverseOnePlusRootHalf = static_cast<float>(1.0 / (M_SQRT1_2 + 1.0));
  static constexpr float kInverseRootTwoPlusOne = static_cast<float>(1.0 / (M_SQRT2 + 1.0));
};

// 4 -> 1 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 4 && OutputChannelCount == 1)>> {
 public:
  inline float Map(const InputSampleType* input, [[maybe_unused]] size_t output_channel) {
    if constexpr (kEnable4ChannelWorkaround) {
      // TODO(fxbug.dev/85201): Temporarily ignore the third and fourth channels.
      return 0.5f * (SampleConverter<InputSampleType>::ToFloat(input[0]) +
                     SampleConverter<InputSampleType>::ToFloat(input[1]));
    }
    return 0.25f * (SampleConverter<InputSampleType>::ToFloat(input[0]) +
                    SampleConverter<InputSampleType>::ToFloat(input[1]) +
                    SampleConverter<InputSampleType>::ToFloat(input[2]) +
                    SampleConverter<InputSampleType>::ToFloat(input[3]));
  }
};

// 4 -> 2 channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<
    InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable*/ false,
    typename std::enable_if_t<(InputChannelCount == 4 && OutputChannelCount == 2)>> {
 public:
  inline float Map(const InputSampleType* input, size_t output_channel) {
    if constexpr (kEnable4ChannelWorkaround) {
      // TODO(fxbug.dev/85201): Temporarily ignore the third and fourth channels.
      return SampleConverter<InputSampleType>::ToFloat(input[output_channel]);
    }
    return 0.5f * (SampleConverter<InputSampleType>::ToFloat(input[output_channel]) +
                   SampleConverter<InputSampleType>::ToFloat(input[output_channel + 2]));
  }
};

// M -> N customizable channel mapper.
template <typename InputSampleType, size_t InputChannelCount, size_t OutputChannelCount>
class ChannelMapper<InputSampleType, InputChannelCount, OutputChannelCount, /*Customizable=*/true> {
 public:
  explicit ChannelMapper(
      std::array<std::array<float, InputChannelCount>, OutputChannelCount> coefficients)
      : coefficients_(std::move(coefficients)) {}

  inline float Map(const InputSampleType* input, size_t output_channel) {
    float output = 0.0f;
    for (size_t input_channel = 0; input_channel < InputChannelCount; ++input_channel) {
      output += coefficients_[output_channel][input_channel] *
                SampleConverter<InputSampleType>::ToFloat(input[input_channel]);
    }
    return output;
  }

 private:
  // Normalized channel coefficients.
  std::array<std::array<float, InputChannelCount>, OutputChannelCount> coefficients_;
};

}  // namespace media_audio

#endif  //  SRC_MEDIA_AUDIO_LIB_FORMAT_2_CHANNEL_MAPPER_H_
