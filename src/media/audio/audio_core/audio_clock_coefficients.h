// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_

#include <lib/zx/time.h>

#include "src/media/audio/lib/clock/pid_control.h"

namespace media::audio {

// Constants related to clock PID-tuning
//
// PID coefficients were determined empirically by the generally-accepted Ziegler-Nichols method:
// determine a P factor (without I or D) leading to steady-state non-divergent oscillation, then
// half it. Set I to ~(2P)/OscillationPeriod, and D to ~(P/8)*OscillationPeriod.
//
// Latest coefficient tuning: 2020-Oct-27.

// Micro-SRC synchronization
//
constexpr double kMicroSrcOscillationPeriod = ZX_MSEC(20);
constexpr double kMicroSrcPFactor = -0.00000007001;
constexpr clock::PidControl::Coefficients kPidFactorsMicroSrc = {
    .proportional_factor = kMicroSrcPFactor,
    .integral_factor = kMicroSrcPFactor * 2 / kMicroSrcOscillationPeriod,
    .derivative_factor = kMicroSrcPFactor * kMicroSrcOscillationPeriod / 8,
};

constexpr clock::PidControl::Coefficients kPidFactorsAdjustClientClock = {
    .proportional_factor = 0,
    .integral_factor = 0,
    .derivative_factor = 0,
};

constexpr double kAdjustDeviceOscillationPeriod = ZX_MSEC(1000);
constexpr double kAdjustDevicePFactor = 0.000000001001;
constexpr clock::PidControl::Coefficients kPidFactorsAdjustDeviceClock = {
    .proportional_factor = kAdjustDevicePFactor,
    .integral_factor = kAdjustDevicePFactor * 2 / kAdjustDeviceOscillationPeriod,
    .derivative_factor = kAdjustDevicePFactor * kAdjustDeviceOscillationPeriod / 8,
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_COEFFICIENTS_H_
