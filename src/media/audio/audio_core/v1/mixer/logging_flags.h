// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_LOGGING_FLAGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_LOGGING_FLAGS_H_

#include <cstdint>

namespace media::audio {

inline constexpr bool kLogGainSetGainCalls = false;
inline constexpr bool kLogGainScaleValues = false;

inline constexpr bool kLogGainSetMute = false;

inline constexpr bool kLogGainSetRamp = false;
inline constexpr bool kLogGainRampAdvance = false;

// This is very verbose and should only be used when no ongoing streams are running.
inline constexpr bool kLogGainScaleCalculation = false;

// Debug computation of output values (ComputeSample), from coefficients and input values.
// Extremely verbose, only useful in a controlled unittest setting.
inline constexpr bool kTraceFilterComputation = false;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_LOGGING_FLAGS_H_
