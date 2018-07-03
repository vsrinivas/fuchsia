// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_MIXER_UTILS_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_MIXER_UTILS_H_

#include <cmath>
#include <limits>
#include <type_traits>

#include <fbl/algorithm.h>
#include <zircon/compiler.h>

#include "garnet/bin/media/audio_server/constants.h"
#include "garnet/bin/media/audio_server/gain.h"

namespace media {
namespace audio {
namespace mixer {

// mixer_utils.h is a collection of inline templated utility functions meant to
// be used by mixer implementations and expanded/optimized at compile time in
// order to produce efficient inner mixing loops for all of the different
// variations of source/destination sample type/channel counts.

// Enum used to differentiate between different scaling optimization types.
enum class ScalerType {
  MUTED,     // Massive attenuation. Just skip data.
  NE_UNITY,  // Non-unity non-zero gain. Scaling is needed.
  EQ_UNITY,  // Unity gain. Scaling is not needed.
};

// Template to read and normalize samples into float32 [ -1.0 , 1.0 ] format.
template <typename SType, typename Enable = void>
class SampleNormalizer;

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, uint8_t>::value>::type> {
 public:
  static inline float Read(const SType* src) {
    return kInt8ToFloat * (static_cast<int32_t>(*src) - kOffsetInt8ToUint8);
  }
};

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, int16_t>::value>::type> {
 public:
  static inline float Read(const SType* src) { return kInt16ToFloat * (*src); }
};

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, int32_t>::value>::type> {
 public:
  static inline float Read(const SType* src) { return kInt24In32ToFloat * (*src); }
};

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, float>::value>::type> {
 public:
  static inline float Read(const SType* src) { return *src; }
};

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
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::NE_UNITY)>::type> {
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

// Template to read normalized source samples, and combine channels if required.
template <typename SType, size_t SChCount, size_t DChCount,
          typename Enable = void>
class SrcReader;

template <typename SType, size_t SChCount, size_t DChCount>
class SrcReader<
    SType, SChCount, DChCount,
    typename std::enable_if<(SChCount == DChCount) ||
                                ((SChCount == 1) && (DChCount == 2)),
                            void>::type> {
 public:
  static constexpr size_t DstPerSrc = DChCount / SChCount;
  static inline float Read(const SType* src) {
    return SampleNormalizer<SType>::Read(src);
  }
};

template <typename SType, size_t SChCount, size_t DChCount>
class SrcReader<
    SType, SChCount, DChCount,
    typename std::enable_if<(SChCount == 2) && (DChCount == 1)>::type> {
 public:
  static constexpr size_t DstPerSrc = 1;
  static inline float Read(const SType* src) {
    return 0.5f * (SampleNormalizer<SType>::Read(src + 0) +
                   SampleNormalizer<SType>::Read(src + 1));
  }
};

// Template to mix normalized destination samples with normalized source samples
// based on scaling and accumulation policy.
template <ScalerType ScaleType, bool DoAccumulate, typename Enable = void>
class DstMixer;

template <ScalerType ScaleType, bool DoAccumulate>
class DstMixer<ScaleType, DoAccumulate,
               typename std::enable_if<DoAccumulate == false>::type> {
 public:
  static inline constexpr float Mix(float, float sample, Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale);
  }
};

template <ScalerType ScaleType, bool DoAccumulate>
class DstMixer<ScaleType, DoAccumulate,
               typename std::enable_if<DoAccumulate == true>::type> {
 public:
  static inline constexpr float Mix(float dst, float sample,
                                    Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale) + dst;
  }
};

}  // namespace mixer
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_MIXER_UTILS_H_
