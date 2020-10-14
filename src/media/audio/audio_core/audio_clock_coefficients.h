// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_

#include "src/media/audio/lib/clock/pid_control.h"

namespace media::audio {

//
// Constants related to PID and clock-tuning
//
// These values were determined empirically based on one accepted rule-of-thumb for setting PID
// factors (Ziegler-Nichols). First discover the P factor (without I or D factors) that leads to
// steady-state non-divergent oscillation, then half that value. Set the I factor to approximately
// (2P)/OscillationPeriod. Set the D factor to approximately (P/8)*OscillationPeriod.

// Micro-SRC synchronization
//
constexpr double kMicroSrcOscillationPeriod = 3840;  // frames
constexpr double kMicroSrcPFactor = -0.00000007611;
constexpr clock::PidControl::Coefficients kPidFactorsMicroSrc = {
    .proportional_factor = kMicroSrcPFactor,
    .integral_factor = kMicroSrcPFactor * 2 / kMicroSrcOscillationPeriod,
    .derivative_factor = kMicroSrcPFactor * kMicroSrcOscillationPeriod / 8};

constexpr clock::PidControl::Coefficients kPidFactorsAdjustClientClock = {
    .proportional_factor = 0, .integral_factor = 0, .derivative_factor = 0};

constexpr clock::PidControl::Coefficients kPidFactorsAdjustHardwareClock = {
    .proportional_factor = 0, .integral_factor = 0, .derivative_factor = 0};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_
