// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_PROCESSING_GAIN_H_
#define SRC_MEDIA_AUDIO_LIB_PROCESSING_GAIN_H_

#include <algorithm>
#include <cmath>

namespace media_audio {

// Minimum gain value below which the gain factor is assumed to be perceived as inaudible.
inline constexpr float kMinGainDb = -160.0f;
inline constexpr float kMinGainScale = 10e-9f;  // equivalent to `DbToScale(kMinGainDb)`

// Unity gain value at which the gain factor is assumed to have no effect.
inline constexpr float kUnityGainDb = 0.0f;
inline constexpr float kUnityGainScale = 1.0f;  // equivalent to `DbToScale(kUnityGainDb)`

// Gain type to differentiate between different optimization methods while processing.
enum class GainType {
  kSilent,    // Gain is effectively silent (either due to muting or massive attenuation).
  kNonUnity,  // Constant non-unity and non-silent gain.
  kUnity,     // Constant unity gain.
  kRamping,   // Non-constant ramping gain.
};

// Applies gain `scale` to `value` with `Type` specific optimizations.
template <GainType Type>
inline float ApplyGain(float value, float scale) {
  if constexpr (Type == GainType::kSilent) {
    return 0.0f;
  } else if constexpr (Type == GainType::kUnity) {
    return value;
  } else {
    return scale * value;
  }
}

// Converts gain `db` to scale.
inline float DbToScale(float db) {
  return (db > kMinGainDb) ? static_cast<float>(std::pow(10.0, static_cast<double>(db) * 0.05))
                           : 0.0f;
}

// Converts gain `scale` to decibels.
inline float ScaleToDb(float scale) {
  return (scale > kMinGainScale) ? std::log10(scale) * 20.0f : kMinGainDb;
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_PROCESSING_GAIN_H_
