// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT2_SAMPLE_CONVERTER_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT2_SAMPLE_CONVERTER_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace media_audio {

// Conversion constants for `SampleType::kUint8`.
inline constexpr int8_t kMaxInt8 = std::numeric_limits<int8_t>::max();
inline constexpr int8_t kMinInt8 = std::numeric_limits<int8_t>::min();
inline constexpr int32_t kFloatToInt8 = -kMinInt8;
inline constexpr int32_t kInt8ToUint8 = std::numeric_limits<uint8_t>::min() - kMinInt8;
inline constexpr float kInt8ToFloat = 1.0f / static_cast<float>(kFloatToInt8);

// Conversion constants for `SampleType::kInt16`.
inline constexpr int16_t kMaxInt16 = std::numeric_limits<int16_t>::max();
inline constexpr int16_t kMinInt16 = std::numeric_limits<int16_t>::min();
inline constexpr int32_t kFloatToInt16 = -kMinInt16;
inline constexpr float kInt16ToFloat = 1.0f / static_cast<float>(kFloatToInt16);

// Conversion constants for `SampleType::kInt32`.
// TODO(fxbug.dev/114920): should we switch to pure int32? this code is kept for backwards
// compatibility, where the old APIs used int24in32, not int32.
inline constexpr int32_t kMaxInt24 = std::numeric_limits<int32_t>::max() >> 8;
inline constexpr int32_t kMinInt24 = std::numeric_limits<int32_t>::min() >> 8;
inline constexpr int32_t kMaxInt24In32 = kMaxInt24 * 0x100;
inline constexpr int32_t kMinInt24In32 = kMinInt24 * 0x100;
inline constexpr int32_t kFloatToInt24 = -kMinInt24;
inline constexpr double kInt24ToFloat = 1.0 / kFloatToInt24;
inline constexpr int64_t kFloatToInt24In32 = -static_cast<int64_t>(kMinInt24In32);

// Template to convert a sample of `SampleType` from/to a normalized 32-bit float format.
// Methods are:
//
// ```
// static inline constexpr SampleType FromFloat(float sample);
// static inline constexpr float ToFloat(SampleType sample);
// ```
template <typename SampleType>
struct SampleConverter;

// Sample converter for `SampleType::kUnsigned8`.
template <>
struct SampleConverter<uint8_t> {
  static inline constexpr uint8_t FromFloat(float sample) {
    return static_cast<uint8_t>(
        std::clamp<int16_t>(static_cast<int16_t>(std::round(sample * kFloatToInt8)), kMinInt8,
                            kMaxInt8) +
        kInt8ToUint8);
  }
  static inline constexpr float ToFloat(uint8_t sample) {
    return static_cast<float>(static_cast<int32_t>(sample) - kInt8ToUint8) * kInt8ToFloat;
  }
};

// Sample converter for `SampleType::kSigned16`.
template <>
struct SampleConverter<int16_t> {
  static inline constexpr int16_t FromFloat(float sample) {
    return static_cast<int16_t>(std::clamp<int32_t>(
        static_cast<int32_t>(std::round(sample * kFloatToInt16)), kMinInt16, kMaxInt16));
  }
  static inline constexpr float ToFloat(int16_t sample) {
    return static_cast<float>(sample) * kInt16ToFloat;
  }
};

// Sample converter for `SampleType::kSigned24In32`.
template <>
struct SampleConverter<int32_t> {
  static inline constexpr int32_t FromFloat(float sample) {
    return std::clamp(static_cast<int32_t>(std::lroundf(sample * kFloatToInt24)), kMinInt24,
                      kMaxInt24) *
           0x100;
  }
  static inline constexpr float ToFloat(int32_t sample) {
    return static_cast<float>(kInt24ToFloat * static_cast<double>(sample >> 8));
  }
};

// Sample converter for `SampleType::kFloat`.
template <>
struct SampleConverter<float> {
  static inline constexpr float FromFloat(float sample) { return sample; }
  static inline constexpr float ToFloat(float sample) { return sample; }
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT2_SAMPLE_CONVERTER_H_
