// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_COEFFICIENTS_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_COEFFICIENTS_H_

#include <lib/zx/time.h>

#include "src/media/audio/lib/clock/pid_control.h"

namespace media::audio {

// ### Constants related to the PID controls that reconcile audio clocks
//
// Proportional-Integral-Derivative controls (PIDs) apply an optimal amount of feedback, for systems
// with well-characterized response. We use PID controls to smoothly reach and maintain tight
// synchronization between audio streams.
//
// To be synchronized, streams must match in _position_ (not just _rate_). Audio streams are
// governed by reference clocks, whose rates can be changed at any time without notification. We
// learn of clock rates/positions only by polling, which suggests a PID-based solution or some other
// form of continuous feedback.
//
// Our PIDs have an input of "position error" and an output of rate adjustment in parts-per-million.
// We define position error as the difference between (a) IN-USE position (from long-running
// sample-rate conversion), and (b) EXPECTED position (calculated from the two reference clocks).
//
// In some modes we apply the rate-adjustment feedback in ways that affect (a); in others we use it
// for adjustments that affect (b).
//
// ## Micro-SRC Synchronization
//
// In this mode, we tune an extra SRC factor that we add to any static SRC, to compensate for rate
// differences between source and destination clocks that we cannot rate-adjust. MicroSrc adjusts
// (a). A positive error implies that SRC should slow down. Thus kMicroSrcPFactor is NEGATIVE.
//
// ## Tuning Adjustable Clocks
//
// In this mode, we tune an audio clock directly, to "chase" another clock. Here we adjust (b). A
// positive position error means the source clock should speed up. Thus kClockChasesClockPFactor
// is POSITIVE.
//
// Side note: if the adjustable clock is a _destination_ clock, logic elsewhere inverts the impact
// of this PID. Upon a positive position error we should _slow down_ the destination clock, thereby
// increasing the source/dest clock ratio that determines (b) -- relative to the step_size that
// determines (a).
//
// In this mode and the previous one, we expect to synchronize once every mix period, leading to
// a worst-case oscillation period of twice that, or 20 msec (based on current 10 msec mix). This is
// seen in kMicroSrcOscillationPeriod and kClockChasesClockOscillationPeriod.
//
// ## Recovering Device Clocks
//
// In this mode, we create a clock that represents an audio device, and we tune that clock to
// "chase" the device's actual position (which can drift over time). The ClockChasesDevice mode
// resembles the earlier ClockChasesClock mode, but differs based on the expected magnitudes of
// client clock adjustments, and the gradual nature of inter-clock drift.
//
// We synthesize a device clock from an ongoing series of [position, monotonic_time] pairs emitted
// by the driver. Here (a) is the last position reported, and (b) is the position computed by our
// synthesized clock for the corresponding monotonic time. We are adjusting (b), so a positive
// position error means our clock is too slow. Thus kClockChasesDevicePFactor is POSITIVE.
//
// We instruct the device to emit two position notifications per ring buffer, and ring buffers
// are generally 500-1000 milliseconds. Assuming 500-msec notifications, our worst-case oscillation
// period would be twice that, or 1000 msec.
//
// ## Actual PID Coefficient Values
//
// PID coefficients were determined empirically by the generally-accepted Ziegler-Nichols method:
// find a proportional value (I and D set to 0) leading to steady-state non-divergent oscillation.
// Set P to half that value, I to ~(2P)/OscillationPeriod, and D to ~(P/8)*OscillationPeriod.
//
// Latest coefficient tuning: 2020-Oct-30.

inline constexpr double kMicroSrcOscillationPeriod = ZX_MSEC(20);
inline constexpr double kMicroSrcPFactor = -0.00000007001;
inline constexpr clock::PidControl::Coefficients kPidFactorsMicroSrc = {
    .proportional_factor = kMicroSrcPFactor,
    .integral_factor = kMicroSrcPFactor * 2 / kMicroSrcOscillationPeriod,
    .derivative_factor = kMicroSrcPFactor * kMicroSrcOscillationPeriod / 8,
};

inline constexpr double kClockChasesClockOscillationPeriod = ZX_MSEC(20);
inline constexpr double kClockChasesClockPFactor = 0.000000007998;
inline constexpr clock::PidControl::Coefficients kPidFactorsClockChasesClock = {
    .proportional_factor = kClockChasesClockPFactor,
    .integral_factor = kClockChasesClockPFactor * 2 / kClockChasesClockOscillationPeriod,
    .derivative_factor = kClockChasesClockPFactor * kClockChasesClockOscillationPeriod / 8,
};

inline constexpr double kClockChasesDeviceOscillationPeriod = ZX_MSEC(1000);
inline constexpr double kClockChasesDevicePFactor = 0.0000000002001;
inline constexpr clock::PidControl::Coefficients kPidFactorsClockChasesDevice = {
    .proportional_factor = kClockChasesDevicePFactor,
    .integral_factor = kClockChasesDevicePFactor * 2 / kClockChasesDeviceOscillationPeriod,
    .derivative_factor = kClockChasesDevicePFactor * kClockChasesDeviceOscillationPeriod / 8,
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_COEFFICIENTS_H_
