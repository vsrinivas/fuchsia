// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_2_SAMPLE_CONVERTER_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_2_SAMPLE_CONVERTER_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace media_audio {

// Conversion constants for `AudioSampleFormat::kUnsigned8`.
inline constexpr int8_t kMaxInt8 = std::numeric_limits<int8_t>::max();
inline constexpr int8_t kMinInt8 = std::numeric_limits<int8_t>::min();
inline constexpr int32_t kFloatToInt8 = -kMinInt8;
inline constexpr int32_t kInt8ToUint8 = std::numeric_limits<uint8_t>::min() - kMinInt8;
inline constexpr float kInt8ToFloat = 1.0f / static_cast<float>(kFloatToInt8);

// Conversion constants for `AudioSampleFormat::kSigned16`.
inline constexpr int16_t kMaxInt16 = std::numeric_limits<int16_t>::max();
inline constexpr int16_t kMinInt16 = std::numeric_limits<int16_t>::min();
inline constexpr int32_t kFloatToInt16 = -kMinInt16;
inline constexpr float kInt16ToFloat = 1.0f / static_cast<float>(kFloatToInt16);

// Conversion constants for `AudioSampleFormat::kSigned24In32`.
inline constexpr int32_t kMaxInt24 = std::numeric_limits<int32_t>::max() >> 8;
inline constexpr int32_t kMinInt24 = std::numeric_limits<int32_t>::min() >> 8;
inline constexpr int32_t kMaxInt24In32 = kMaxInt24 * 0x100;
inline constexpr int32_t kMinInt24In32 = kMinInt24 * 0x100;
inline constexpr int32_t kFloatToInt24 = -kMinInt24;
inline constexpr double kInt24ToFloat = 1.0 / kFloatToInt24;
inline constexpr int64_t kFloatToInt24In32 = -static_cast<int64_t>(kMinInt24In32);

// Template to convert a sample s `SampleType` from/to a normalized 32-bit float format.
template <typename SampleType, typename Trait = void>
struct SampleConverter;

// Sample converter for `AudioSampleFormat::kUnsigned8`.
template <typename SampleType>
struct SampleConverter<SampleType, typename std::enable_if_t<std::is_same_v<SampleType, uint8_t>>> {
  static inline constexpr SampleType FromFloat(float sample) {
    return static_cast<uint8_t>(
        std::clamp<int16_t>(static_cast<int16_t>(std::round(sample * kFloatToInt8)), kMinInt8,
                            kMaxInt8) +
        kInt8ToUint8);
  }
  static inline constexpr float ToFloat(SampleType sample) {
    return static_cast<float>(static_cast<int32_t>(sample) - kInt8ToUint8) * kInt8ToFloat;
  }
};

// Sample converter for `AudioSampleFormat::kSigned16`.
template <typename SampleType>
struct SampleConverter<SampleType, typename std::enable_if_t<std::is_same_v<SampleType, int16_t>>> {
  static inline constexpr SampleType FromFloat(float sample) {
    return static_cast<int16_t>(std::clamp<int32_t>(
        static_cast<int32_t>(std::round(sample * kFloatToInt16)), kMinInt16, kMaxInt16));
  }
  static inline constexpr float ToFloat(SampleType sample) {
    return static_cast<float>(sample) * kInt16ToFloat;
  }
};

// Sample converter for `AudioSampleFormat::kSigned24In32`.
template <typename SampleType>
struct SampleConverter<SampleType, typename std::enable_if_t<std::is_same_v<SampleType, int32_t>>> {
  static inline constexpr SampleType FromFloat(float sample) {
    return std::clamp(static_cast<int32_t>(std::lroundf(sample * kFloatToInt24)), kMinInt24,
                      kMaxInt24) *
           0x100;
  }
  static inline constexpr float ToFloat(SampleType sample) {
    return static_cast<float>(kInt24ToFloat * static_cast<double>(sample >> 8));
  }
};

// Sample converter for `AudioSampleFormat::kFloat`.
template <typename SampleType>
struct SampleConverter<SampleType, typename std::enable_if_t<std::is_same_v<SampleType, float>>> {
  static inline constexpr SampleType FromFloat(float sample) {
    return std::clamp(sample, -1.0f, 1.0f);
  }
  static inline constexpr float ToFloat(SampleType sample) {
    // TODO(fxbug.dev/87651): This is currently *not* normalized to keep the existing audio_core
    // functionality as-is, but it could be safer to normalize this to [-1, 1] range as well moving
    // forward (once the audio_core dependencies are deprecated).
    return sample;
  }
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_2_SAMPLE_CONVERTER_H_
