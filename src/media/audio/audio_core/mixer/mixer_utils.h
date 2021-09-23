// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_

#include <cmath>
#include <type_traits>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {

constexpr bool kChannelMap4to2Workaround = true;
// TODO(fxbug.dev/85201): Remove this workaround, once the device properly maps channels.

namespace mixer {

// mixer_utils.h is a collection of inline templated utility functions meant to
// be used by mixer implementations and expanded/optimized at compile time in
// order to produce efficient inner mixing loops for all of the different
// variations of source/destination sample type/channel counts.

//
// ScalerType
//
// Enum used to differentiate between different scaling optimization types.
enum class ScalerType {
  MUTED,     // Massive attenuation. Just skip data.
  NE_UNITY,  // Non-unity non-zero gain. Scaling is needed.
  EQ_UNITY,  // Unity gain. Scaling is not needed.
  RAMPING,   // Scaling is needed, using a non-constant scaler value
};

//
// SampleNormalizer
//
// Template to read and normalize samples into float32 [ -1.0 , 1.0 ] format.
template <typename SourceSampleType, typename Enable = void>
class SampleNormalizer;

template <typename SourceSampleType>
class SampleNormalizer<SourceSampleType,
                       typename std::enable_if_t<std::is_same_v<SourceSampleType, uint8_t>>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr) {
    return kInt8ToFloat *
           static_cast<float>(static_cast<int32_t>(*source_ptr) - kOffsetInt8ToUint8);
  }
};

template <typename SourceSampleType>
class SampleNormalizer<SourceSampleType,
                       typename std::enable_if_t<std::is_same_v<SourceSampleType, int16_t>>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr) {
    return kInt16ToFloat * (*source_ptr);
  }
};

template <typename SourceSampleType>
class SampleNormalizer<SourceSampleType,
                       typename std::enable_if_t<std::is_same_v<SourceSampleType, int32_t>>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr) {
    return static_cast<float>(kInt24ToFloat * ((*source_ptr) >> 8));
  }
};

template <typename SourceSampleType>
class SampleNormalizer<SourceSampleType,
                       typename std::enable_if_t<std::is_same_v<SourceSampleType, float>>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr) { return *source_ptr; }
};

//
// SampleScaler
//
// Template used to scale normalized sample vals by supplied amplitude scalers.
template <ScalerType ScaleType, typename Enable = void>
class SampleScaler;

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if_t<(ScaleType == ScalerType::MUTED)>> {
 public:
  static inline float Scale(float, Gain::AScale) { return 0.0f; }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if_t<(ScaleType == ScalerType::NE_UNITY) ||
                                                        (ScaleType == ScalerType::RAMPING)>> {
 public:
  static inline float Scale(float val, Gain::AScale scale) { return scale * val; }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if_t<(ScaleType == ScalerType::EQ_UNITY)>> {
 public:
  static inline float Scale(float val, Gain::AScale) { return val; }
};

//
// SourceReader
//
// Template to read normalized source samples, and combine channels if required.
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount,
          typename Enable = void>
class SourceReader;

// N:N mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<(SourceChanCount == DestChanCount)>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan);
  }
};

// 1:N mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<
    SourceSampleType, SourceChanCount, DestChanCount,
    typename std::enable_if_t<((SourceChanCount == 1) && (SourceChanCount != DestChanCount))>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return SampleNormalizer<SourceSampleType>::Read(source_ptr);
  }
};

// Mappers for 2-channel sources
//
// 2->1 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<(SourceChanCount == 2) && (DestChanCount == 1)>> {
 public:
  // This simple 2:1 channel mapping assumes a "LR" stereo configuration for the source channels.
  // Each dest frame's single value is essentially the average of the 2 source chans.
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return 0.5f * (SampleNormalizer<SourceSampleType>::Read(source_ptr + 0) +
                   SampleNormalizer<SourceSampleType>::Read(source_ptr + 1));
  }
};

// 2->3 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 2) && (DestChanCount == 3))>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return (dest_chan < 2 ? SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan)
                          : 0.5f * (SampleNormalizer<SourceSampleType>::Read(source_ptr) +
                                    SampleNormalizer<SourceSampleType>::Read(source_ptr + 1)));
  }
};

// 2->4 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 2) && (DestChanCount == 4))>> {
 public:
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan % 2);
  }
};

// Mappers for 3-channel sources
//
// 3->1 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 3) && (DestChanCount == 1))>> {
 public:
  // This simple 3:1 channel mapping assumes an equal weighting of the 3 source channels.
  // Each dest frame's single value is essentially the average of the 3 source chans.
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return (SampleNormalizer<SourceSampleType>::Read(source_ptr + 0) +
            SampleNormalizer<SourceSampleType>::Read(source_ptr + 1) +
            SampleNormalizer<SourceSampleType>::Read(source_ptr + 2)) /
           3.0f;
  }
};

// 3->2 mapper
constexpr auto kOnePlusRootHalf = static_cast<float>(M_SQRT1_2 + 1.0);
// 1.70710678118654752
constexpr auto kInverseOnePlusRootHalf = static_cast<float>(1.0 / (M_SQRT1_2 + 1.0));
// 0.58578643762690495
constexpr auto kInverseRootTwoPlusOne = static_cast<float>(1.0 / (M_SQRT2 + 1.0));

template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 3) && (DestChanCount == 2))>> {
 public:
  // This simple 3:2 channel mapping assumes a "LRC" configuration for the 3 source channels. Thus
  // in each 3-chan source frame and 2-chan dest frame, we mix source chans 0+2 to dest chan 0, and
  // source chans 1+2 to dest chan 1. Because we mix it equally into two dest channels, we multiply
  // source chan2 by sqr(.5) to maintain an equal-power contribution compared to source chans 0&1.
  // Finally, normalize both dest chans (divide by max possible value) to keep result within bounds:
  // "divide by 1+sqr(0.5)" is optimized to "multiply by kInverseOnePlusRootHalf".
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return kInverseOnePlusRootHalf *
               SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan) +
           kInverseRootTwoPlusOne * SampleNormalizer<SourceSampleType>::Read(source_ptr + 2);
  }
};

// Mappers for 4-channel sources
//
// 4->1 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 4) && (DestChanCount == 1))>> {
 public:
  // This simple 4:1 channel mapping averages the incoming 4 source channels to determine the value
  // for the lone destination channel.
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    return 0.25f * (SampleNormalizer<SourceSampleType>::Read(source_ptr + 0) +
                    SampleNormalizer<SourceSampleType>::Read(source_ptr + 1) +
                    SampleNormalizer<SourceSampleType>::Read(source_ptr + 2) +
                    SampleNormalizer<SourceSampleType>::Read(source_ptr + 3));
  }
};

// 4->2 mapper
template <typename SourceSampleType, size_t SourceChanCount, size_t DestChanCount>
class SourceReader<SourceSampleType, SourceChanCount, DestChanCount,
                   typename std::enable_if_t<((SourceChanCount == 4) && (DestChanCount == 2))>> {
 public:
  // This simple 4:2 channel mapping assumes a "LRLR" configuration for the 4 source channels (e.g.
  // a "four corners" Quad config: FrontL|FrontR|BackL|BackR). Thus in each 4-chan source frame and
  // 2-chan dest frame, we mix source chans 0+2 to dest chan 0, and source chans 1+3 to dest chan 1.
  static inline float Read(const SourceSampleType* source_ptr, size_t dest_chan) {
    if constexpr (kChannelMap4to2Workaround) {
      // As a temporary measure, ignore channels 2 and 3.
      // TODO(fxbug.dev/85201): Remove this workaround, once the device properly maps channels.
      return SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan);
    } else {
      return 0.5f * (SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan) +
                     SampleNormalizer<SourceSampleType>::Read(source_ptr + dest_chan + 2));
    }
  }
};

//
// Interpolation variants
//
// Fixed::Format::FractionalBits is 13 for Fixed types, so max alpha of "1.0" is 0x00002000.
constexpr float kFramesPerPtsSubframe = 1.0f / (1 << kPtsFractionalBits);

// First-order Linear Interpolation formula (Position-fraction):
//   out = Pf(S' - S) + S
inline float LinearInterpolate(float A, float B, float alpha) { return ((B - A) * alpha) + A; }

//
// DestMixer
//
// Template to mix normalized destination samples with normalized source samples
// based on scaling and accumulation policy.
template <ScalerType ScaleType, bool DoAccumulate, typename Enable = void>
class DestMixer;

template <ScalerType ScaleType, bool DoAccumulate>
class DestMixer<ScaleType, DoAccumulate, typename std::enable_if_t<DoAccumulate == false>> {
 public:
  static inline constexpr float Mix(float, float sample, Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale);
  }
};

template <ScalerType ScaleType, bool DoAccumulate>
class DestMixer<ScaleType, DoAccumulate, typename std::enable_if_t<DoAccumulate == true>> {
 public:
  static inline constexpr float Mix(float dest, float sample, Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale) + dest;
  }
};

}  // namespace mixer
}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_
