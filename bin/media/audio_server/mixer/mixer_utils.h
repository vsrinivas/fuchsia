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

// Template to read samples and normalize them into signed 18-bit integers
// stored in 32 bit integers.
template <typename SType, typename Enable = void>
class SampleNormalizer;

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, uint8_t>::value>::type> {
 public:
  static inline int32_t Read(const SType* src) {
    return (static_cast<int32_t>(*src) - 0x80) << (kAudioPipelineWidth - 8);
  }
};

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, int16_t>::value>::type> {
 public:
  // Called frequently; expecting optimizing compiler to take advantage of
  // constexpr kAudioPipelineWidth to eliminate the conditional.
  static inline int32_t Read(const SType* src) {
    return (kAudioPipelineWidth == 16)
               ? static_cast<int32_t>(*src)
               : static_cast<int32_t>(*src) << (kAudioPipelineWidth - 16);
  }
};

template <typename SType>
class SampleNormalizer<
    SType, typename std::enable_if<std::is_same<SType, float>::value>::type> {
 public:
  static inline int32_t Read(const SType* src) {
    // 1. constrain value to [-1.0, +1.0]; 2. scale to fixed-point nominal range
    // ([-32768, +32768], see below); 3. round; 4. return the int portion.
    //
    // Converting audio between float and int is surprisingly controversial.
    // (blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there, others).
    // Our method DOES allow an incoming value of +1.0, which for integer-based
    // outputs will EVENTUALLY be clamped if not attenuated earlier. That said,
    // the "practically clipping" value of +1.0 is rare in WAV files, and other
    // sources should easily be able to reduce their input levels.
    SType val = fbl::clamp<SType>(*src, -1.0f, 1.0f);
    val *= (1 << (kAudioPipelineWidth - 1));
    return static_cast<int32_t>(round(val));
  }
};

// Template used to scale normalized sample vals by supplied amplitude scalers.
template <ScalerType ScaleType, typename Enable = void>
class SampleScaler;

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::MUTED)>::type> {
 public:
  static inline int32_t Scale(int32_t, Gain::AScale) { return 0; }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::NE_UNITY)>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale scale) {
    return round(scale * val);
  }
};

template <ScalerType ScaleType>
class SampleScaler<ScaleType, typename std::enable_if<(
                                  ScaleType == ScalerType::EQ_UNITY)>::type> {
 public:
  static inline int32_t Scale(int32_t val, Gain::AScale) { return val; }
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
  static inline int32_t Read(const SType* src) {
    return SampleNormalizer<SType>::Read(src);
  }
};

template <typename SType, size_t SChCount, size_t DChCount>
class SrcReader<
    SType, SChCount, DChCount,
    typename std::enable_if<(SChCount == 2) && (DChCount == 1)>::type> {
 public:
  static constexpr size_t DstPerSrc = 1;
  static inline int32_t Read(const SType* src) {
    // Before shift, add 1 if positive (right-shift truncates asymmetrically).
    int32_t sum = SampleNormalizer<SType>::Read(src + 0) +
                  SampleNormalizer<SType>::Read(src + 1);
    return (sum > 0 ? sum + 1 : sum) >> 1;
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
  static inline constexpr int32_t Mix(int32_t, int32_t sample,
                                      Gain::AScale scale) {
    return SampleScaler<ScaleType>::Scale(sample, scale);
  }
};

template <ScalerType ScaleType, bool DoAccumulate>
class DstMixer<ScaleType, DoAccumulate,
               typename std::enable_if<DoAccumulate == true>::type> {
 public:
  static inline constexpr int32_t Mix(int32_t dst, int32_t sample,
                                      Gain::AScale scale) {
    // TODO(mpuryear): MTWN-83 Accumulator should clamp to int32.
    return SampleScaler<ScaleType>::Scale(sample, scale) + dst;
  }
};

}  // namespace mixer
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_MIXER_UTILS_H_
