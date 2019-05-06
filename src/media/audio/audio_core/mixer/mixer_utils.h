// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_

#include <type_traits>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio::mixer {

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
template <typename SrcSampleType, typename Enable = void>
class SampleNormalizer;

template <typename SrcSampleType>
class SampleNormalizer<SrcSampleType,
                       typename std::enable_if<
                           std::is_same<SrcSampleType, uint8_t>::value>::type> {
 public:
  static inline float Read(const SrcSampleType* src) {
    return kInt8ToFloat * (static_cast<int32_t>(*src) - kOffsetInt8ToUint8);
  }
};

template <typename SrcSampleType>
class SampleNormalizer<SrcSampleType,
                       typename std::enable_if<
                           std::is_same<SrcSampleType, int16_t>::value>::type> {
 public:
  static inline float Read(const SrcSampleType* src) {
    return kInt16ToFloat * (*src);
  }
};

template <typename SrcSampleType>
class SampleNormalizer<SrcSampleType,
                       typename std::enable_if<
                           std::is_same<SrcSampleType, int32_t>::value>::type> {
 public:
  static inline float Read(const SrcSampleType* src) {
    return kInt24In32ToFloat * (*src);
  }
};

template <typename SrcSampleType>
class SampleNormalizer<
    SrcSampleType,
    typename std::enable_if<std::is_same<SrcSampleType, float>::value>::type> {
 public:
  static inline float Read(const SrcSampleType* src) { return *src; }
};

//
// SampleScaler
//
// Template used to scale normalized sample vals by supplied amplitude scalers.
template <ScalerType ScaleType, typename Enable = void>
class SampleScaler;

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::MUTED)>::type> {
 public:
  static inline float Scale(float, Gain::AScale) { return 0.0f; }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<
                                  (ScaleType == ScalerType::NE_UNITY) ||
                                  (ScaleType == ScalerType::RAMPING)>::type> {
 public:
  static inline float Scale(float val, Gain::AScale scale) {
    return scale * val;
  }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::EQ_UNITY)>::type> {
 public:
  static inline float Scale(float val, Gain::AScale) { return val; }
};

//
// SrcReader
//
// Template to read normalized source samples, and combine channels if required.
template <typename SrcSampleType, size_t SrcChanCount, size_t DestChanCount,
          typename Enable = void>
class SrcReader;

template <typename SrcSampleType, size_t SrcChanCount, size_t DestChanCount>
class SrcReader<
    SrcSampleType, SrcChanCount, DestChanCount,
    typename std::enable_if<(SrcChanCount == DestChanCount) ||
                                ((SrcChanCount == 1) && (DestChanCount == 2)),
                            void>::type> {
 public:
  static constexpr size_t DestPerSrc = DestChanCount / SrcChanCount;
  static inline float Read(const SrcSampleType* src) {
    return SampleNormalizer<SrcSampleType>::Read(src);
  }
};

template <typename SrcSampleType, size_t SrcChanCount, size_t DestChanCount>
class SrcReader<SrcSampleType, SrcChanCount, DestChanCount,
                typename std::enable_if<(SrcChanCount == 2) &&
                                        (DestChanCount == 1)>::type> {
 public:
  static constexpr size_t DestPerSrc = 1;
  static inline float Read(const SrcSampleType* src) {
    return 0.5f * (SampleNormalizer<SrcSampleType>::Read(src + 0) +
                   SampleNormalizer<SrcSampleType>::Read(src + 1));
  }
};

//
// DestMixer
//
// Template to mix normalized destination samples with normalized source samples
// based on scaling and accumulation policy.
template <ScalerType ScaleType, bool DoAccumulate, typename Enable = void>
class DestMixer;

template <ScalerType ScaleType, bool DoAccumulate>
class DestMixer<ScaleType, DoAccumulate,
                typename std::enable_if<DoAccumulate == false>::type> {
 public:
  static inline constexpr float Mix(float, float sample, Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale);
  }
};

template <ScalerType ScaleType, bool DoAccumulate>
class DestMixer<ScaleType, DoAccumulate,
                typename std::enable_if<DoAccumulate == true>::type> {
 public:
  static inline constexpr float Mix(float dest, float sample,
                                    Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale) + dest;
  }
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_MIXER_UTILS_H_
