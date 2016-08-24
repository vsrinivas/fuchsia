// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_AUDIO_PLATFORM_GENERIC_MIXERS_MIXER_UTILS_H_
#define APPS_MEDIA_SERVICES_AUDIO_PLATFORM_GENERIC_MIXERS_MIXER_UTILS_H_

#include <limits>
#include <type_traits>

#include "apps/media/services/audio/gain.h"

namespace mojo {
namespace media {
namespace audio {
namespace mixers {
namespace utils {

// mixer_utils.h is a collection of inline templated utility functions meant to
// be used by mixer implementations and expanded/optimized at compile time in
// order to produce efficient inner mixing loops for all of the different
// variations of source/destination sample type/channel counts.

// Enum used to differentiate between different scaling optimization types.
enum class ScalerType {
  MUTED,     // Massive attenuation.  Just skip data.
  LT_UNITY,  // Less than unity gain.  Scaling is needed, but clipping is not.
  EQ_UNITY,  // Unity gain.  Neither scaling nor clipping is needed.
  GT_UNITY,  // Greater than unity gain.  Scaling and clipping is needed.
};

// Template to read samples and normalize them into signed 16 bit integers
// stored in 32 bit integers.
template <typename SType, typename Enable = void>
class SampleNormalizer;

template <typename SType>
class SampleNormalizer<
    SType,
    typename std::enable_if<std::is_same<SType, uint8_t>::value, void>::type> {
 public:
  static inline int32_t Read(const SType* src) {
    register SType tmp = *src;
    return (static_cast<int32_t>(tmp) << 8) - 0x8000;
  }
};

template <typename SType>
class SampleNormalizer<
    SType,
    typename std::enable_if<std::is_same<SType, int16_t>::value, void>::type> {
 public:
  static inline int32_t Read(const SType* src) {
    return static_cast<int32_t>(*src);
  }
};

// Template used to scale a normalized sample value by the supplied amplitude
// scaler.
template <ScalerType ScaleType, typename Enable = void>
class SampleScaler;

template <ScalerType ScaleType>
class SampleScaler<
    ScaleType,
    typename std::enable_if<(ScaleType == ScalerType::MUTED), void>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale scale) { return 0; }
};

template <ScalerType ScaleType>
class SampleScaler<
    ScaleType,
    typename std::enable_if<(ScaleType == ScalerType::LT_UNITY), void>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale scale) {
    return static_cast<int32_t>((static_cast<int64_t>(val) * scale) >>
                                Gain::FRACTIONAL_BITS);
  }
};

template <ScalerType ScaleType>
class SampleScaler<
    ScaleType,
    typename std::enable_if<(ScaleType == ScalerType::EQ_UNITY), void>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale scale) { return val; }
};

template <ScalerType ScaleType>
class SampleScaler<
    ScaleType,
    typename std::enable_if<(ScaleType == ScalerType::GT_UNITY), void>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale scale) {
    using Limit = std::numeric_limits<int16_t>;

    val = static_cast<int32_t>((static_cast<int64_t>(val) * scale) >>
                               Gain::FRACTIONAL_BITS);

    return (val > Limit::max()) ? Limit::max()
                                : ((val < Limit::min()) ? Limit::min() : val);
  }
};

// Template to read normalized source samples, and combine channels if required.
template <typename SType,
          size_t SChCount,
          size_t DChCount,
          typename Enable = void>
class SrcReader;

template <typename SType, size_t SChCount, size_t DChCount>
class SrcReader<
    SType,
    SChCount,
    DChCount,
    typename std::enable_if<(SChCount == DChCount) ||
                                ((SChCount == 1) && (DChCount == 2)),
                            void>::type> {
 public:
  static constexpr size_t DstPerSrc = DChCount / SChCount;
  static inline int32_t Read(const SType* src) {
    return SampleNormalizer<SType>::Read(src);
  }
};

template <typename SType, size_t SChCount, size_t DChCount>
class SrcReader<
    SType,
    SChCount,
    DChCount,
    typename std::enable_if<(SChCount == 2) && (DChCount == 1), void>::type> {
 public:
  static constexpr size_t DstPerSrc = 1;
  static inline int32_t Read(const SType* src) {
    return (SampleNormalizer<SType>::Read(src + 0) +
            SampleNormalizer<SType>::Read(src + 1)) >>
           1;
  }
};

// Template to mix normalized destination samples with normalized source samples
// based on scaling and accumulation policy.
template <ScalerType ScaleType, bool DoAccumulate, typename Enable = void>
class DstMixer;

template <ScalerType ScaleType, bool DoAccumulate>
class DstMixer<ScaleType,
               DoAccumulate,
               typename std::enable_if<DoAccumulate == false, void>::type> {
 public:
  static inline constexpr int32_t Mix(int32_t dst,
                                      int32_t sample,
                                      Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale);
  }
};

template <ScalerType ScaleType, bool DoAccumulate>
class DstMixer<ScaleType,
               DoAccumulate,
               typename std::enable_if<DoAccumulate == true, void>::type> {
 public:
  static inline constexpr int32_t Mix(int32_t dst,
                                      int32_t sample,
                                      Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale) + dst;
  }
};

}  // namespace utils
}  // namespace mixers
}  // namespace audio
}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_AUDIO_PLATFORM_GENERIC_MIXERS_MIXER_UTILS_H_
