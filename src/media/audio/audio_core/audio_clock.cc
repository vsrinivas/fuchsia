// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>

#include <algorithm>
#include <cmath>
#include <iomanip>

#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

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
constexpr double kMicroSrcOscillationGain = -0.0000002537;
constexpr double kMicroSrcPFactor = kMicroSrcOscillationGain * 0.3;
constexpr clock::PidControl::Coefficients kPidFactorsMicroSrc = {
    .proportional_factor = kMicroSrcPFactor,
    .integral_factor = kMicroSrcPFactor * 2 / kMicroSrcOscillationPeriod,
    .derivative_factor = kMicroSrcPFactor * kMicroSrcOscillationPeriod / 8};

// Unlike an actual zx::clock, micro-SRC synchronization is not bound by the [-1000, +1000]
// parts-per-million range. Micro-SRC fills the GAP between zx::clocks that AudioCore cannot
// directly adjust (they are either adjusted by some other party or simply represent hardware in a
// different clock domain and thus may drift): these clocks can each diverge from the local system
// monotonic clock by as much as +/- 1000 ppm. Micro-SRC may need to reconcile a rate difference of
// 2000 PPM; we set a limit of 2500 to more rapidly eliminate discrepancies that approach 2000 PPM.
constexpr double kMicroSrcRateAdjustmentMax = 0.0025;

//
// static methods
//
AudioClock AudioClock::CreateAsClientNonadjustable(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, false);
  FX_CHECK(audio_clock.is_valid());

  // Next: set micro-SRC PID coefficients, approprate for a software-tuned clock.
  audio_clock.ConfigureAdjustment(kPidFactorsMicroSrc);

  return audio_clock;
}

AudioClock AudioClock::CreateAsClientAdjustable(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, true);
  FX_CHECK(audio_clock.is_valid());

  // Next: set HW-chaser PID coefficients, approprate for an adjustable client clock.

  return audio_clock;
}

AudioClock AudioClock::CreateAsDeviceNonadjustable(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, false, domain);
  FX_CHECK(audio_clock.is_valid());

  return audio_clock;
}

AudioClock AudioClock::CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, true, domain);
  FX_CHECK(audio_clock.is_valid());

  // Next: set HW-response PID coefficients, approprate for a tuned hardware clock.
  return audio_clock;
}

AudioClock::SyncMode AudioClock::SynchronizationMode(AudioClock& clock1, AudioClock& clock2) {
  FX_CHECK(clock1.is_valid());
  FX_CHECK(clock2.is_valid());

  if (clock1 == clock2) {
    return SyncMode::None;
  }
  if (clock1.is_device_clock() && clock2.is_device_clock() && clock1.domain() == clock2.domain()) {
    return SyncMode::None;
  }

  if ((clock1.is_client_clock() && clock1.is_adjustable()) ||
      (clock2.is_client_clock() && clock2.is_adjustable())) {
    return SyncMode::AdjustClientClock;
  }

  if (clock1.is_device_clock() && clock1.is_adjustable() && clock2.controls_device_clock()) {
    return SyncMode::AdjustHardwareClock;
  }
  if (clock2.is_device_clock() && clock2.is_adjustable() && clock1.controls_device_clock()) {
    return SyncMode::AdjustHardwareClock;
  }

  return SyncMode::MicroSrc;
}

AudioClock::AudioClock(zx::clock clock, Source source, bool is_adjustable, uint32_t domain)
    : clock_(std::move(clock)), source_(source), is_adjustable_(is_adjustable), domain_(domain) {
  // If we can read the clock now, we will always be able to read it. This quick check covers all
  // error modes: bad handle, wrong object type, no RIGHT_READ, clock not running.
  zx_time_t now_unused;
  if (clock_.read(&now_unused) != ZX_OK) {
    source_ = Source::Invalid;
    clock_.reset();
  }

  // Check whether a ClientAdjustable zx::clock has write privileges (can be rate-adjusted)?
}

// Set the PID coefficients for this clock.
void AudioClock::ConfigureAdjustment(const clock::PidControl::Coefficients& pid_coefficients) {
  FX_CHECK(is_valid());

  feedback_control_loop_ = clock::PidControl(pid_coefficients);
}

zx::clock AudioClock::DuplicateClock() const {
  FX_CHECK(is_valid());

  // We pre-qualify the clock, so this should never fail.
  return audio::clock::DuplicateClock(clock_).take_value();
}

bool AudioClock::set_controls_device_clock(bool should_control_device_clock) {
  FX_CHECK(is_valid());

  controls_device_clock_ = (is_client_clock() && !is_adjustable() && should_control_device_clock);
  return controls_device_clock_;
}

TimelineFunction AudioClock::ref_clock_to_clock_mono() const {
  FX_CHECK(is_valid());

  // We pre-qualify the clock, so this should never fail.
  return audio::clock::SnapshotClock(clock_).take_value().reference_to_monotonic;
}

zx::time AudioClock::Read() const {
  FX_CHECK(is_valid());

  // We pre-qualify the clock for all the known ways that read can fail.
  zx::time ref_now;
  clock_.read(ref_now.get_address());

  return ref_now;
}

zx::time AudioClock::ReferenceTimeFromMonotonicTime(zx::time mono_time) const {
  FX_CHECK(is_valid());

  // We pre-qualify the clock, so this should never fail.
  return audio::clock::ReferenceTimeFromMonotonicTime(clock_, mono_time).take_value();
}

zx::time AudioClock::MonotonicTimeFromReferenceTime(zx::time ref_time) const {
  FX_CHECK(is_valid());

  // We pre-qualify the clock, so this should never fail.
  return audio::clock::MonotonicTimeFromReferenceTime(clock_, ref_time).take_value();
}

// The time units here are dest frames, the error factor is a difference in frac_src frames.
// This method is periodically called, to incorporate the current source position error (in
// fractional frames) into a feedback control that tunes the rate-adjustment factor for this clock.
void AudioClock::TuneRateForError(int64_t dest_frame, Fixed frac_src_error) {
  FX_CHECK(is_valid());

  FX_CHECK(is_client_clock() || is_adjustable()) << "Cannot rate-adjust this type of clock";

  FX_LOGS(TRACE) << __func__ << "(" << frac_src_error.raw_value() << ", " << dest_frame
                 << ") for clock type " << (is_device_clock() ? "Device " : "Client ")
                 << (is_adjustable_ ? "Adjustable" : "Non-adjustable");

  // Feed into the PID
  feedback_control_loop_.TuneForError(dest_frame, frac_src_error.raw_value());

  // This is a zero-centric adjustment factor, relative to current rate.
  auto adjustment = feedback_control_loop_.Read();

  if (is_adjustable()) {
    if (is_device_clock()) {
      // TODO(fxbug.dev/46648): adjust hardware clock rate by rate_adjustment

      // Convert this to a (truncated) PPM value, set_rate_adjust and update the device clock
    } else {
      // TODO(fxbug.dev/46651): adjust clock by rate_adjustment

      // Convert this to a (truncated) PPM value, set_rate_adjust and update the client clock
    }
  } else {
    // We could support micro-SRC between two device clocks but do not currently do so.
    FX_CHECK(is_client_clock());

    //' adjustment' contains the impact of the current error; adjustment_rate_ can be set now.
    adjustment = trunc(adjustment * 1'000'000) / 1'000'000;
    adjustment = std::clamp(adjustment, -kMicroSrcRateAdjustmentMax, kMicroSrcRateAdjustmentMax);
    auto adjustment_rate = 1.0 + adjustment;

    auto prev_adjustment_rate = adjustment_rate_;
    adjustment_rate_ = TimelineRate(adjustment_rate);

    if (prev_adjustment_rate != adjustment_rate_) {
      FX_LOGS(DEBUG) << "Micro-SRC:" << std::fixed << std::setprecision(2) << std::setw(8)
                     << adjustment * 1'000'000.0 << " ppm; frac_error "
                     << frac_src_error.raw_value();
    }
  }
}

void AudioClock::ResetRateAdjustment(int64_t time) {
  FX_CHECK(is_valid());

  adjustment_rate_ = TimelineRate(1u);
  feedback_control_loop_.Start(time);
}

}  // namespace media::audio
