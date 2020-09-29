// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>

#include <algorithm>
#include <iomanip>

#include "src/media/audio/lib/clock/pid_control.h"

namespace media::audio {

// These values were determined empirically based on one accepted rule-of-thumb for setting PID
// factors. First discover the P factor (without I or D factors) that leads to steady-state
// non-divergent oscillation, then half that value. Set the I factor to approximately
// (2P)/OscillationPeriod. Set the D factor to approximately (P/8)*OscillationPeriod.
//
constexpr double kMicroSrcTypicalOscillationPeriod = 6000;

constexpr double kMicroSrcPFactor = -0.000000125;
constexpr double kMicroSrcIFactor = kMicroSrcPFactor * 2 / kMicroSrcTypicalOscillationPeriod;
constexpr double kMicroSrcDFactor = kMicroSrcPFactor * kMicroSrcTypicalOscillationPeriod / 8;

constexpr clock::PidControl::Coefficients kPidFactorsMicroSrc = {kMicroSrcPFactor, kMicroSrcIFactor,
                                                                 kMicroSrcDFactor};

// static methods
AudioClock AudioClock::CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, Type::Adjustable, domain);

  // Next: set HW-response PID coefficients approprately for a tuned hardware clock.

  return audio_clock;
}

AudioClock AudioClock::CreateAsDeviceStatic(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, Type::NonAdjustable, domain);

  return audio_clock;
}

AudioClock AudioClock::CreateAsOptimal(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, Type::Adjustable);

  // Next: set HW-chaser PID coefficients approprately for an optimal clock.

  return audio_clock;
}

AudioClock AudioClock::CreateAsCustom(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, Type::NonAdjustable);

  // Set micro-SRC PID coefficients approprately for a software-tuned clock.
  audio_clock.ConfigureAdjustment(kPidFactorsMicroSrc);

  return audio_clock;
}

AudioClock::SyncMode AudioClock::SynchronizationMode(AudioClock& clock1, AudioClock& clock2) {
  if (clock1 == clock2) {
    return SyncMode::None;
  }
  if (clock1.is_device_clock() && clock2.is_device_clock() && clock1.domain() == clock2.domain()) {
    return SyncMode::None;
  }

  if (clock1.is_flexible() || clock2.is_flexible()) {
    return SyncMode::AdjustClientClock;
  }

  if (clock1.is_tuneable() && clock2.controls_tuneable_clock()) {
    return SyncMode::TuneHardware;
  }
  if (clock2.is_tuneable() && clock1.controls_tuneable_clock()) {
    return SyncMode::TuneHardware;
  }

  return SyncMode::MicroSrc;
}

AudioClock::AudioClock(zx::clock clock, Source source, Type type, uint32_t domain)
    : clock_(std::move(clock)), source_(source), type_(type), domain_(domain) {
  // If we can read the clock now, we will always be able to read it. This quick check covers all
  // error modes: bad handle, wrong object type, no RIGHT_READ, clock not running.
  zx_time_t ref_now;
  if (clock_.read(&ref_now) != ZX_OK) {
    type_ = Type::Invalid;
    clock_.reset();
  }

  if (is_valid()) {
    auto result = audio::clock::SnapshotClock(clock_);
    FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.
    ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;
  }

  // Check here whether an Adjustable|Client zx::clock has write privileges (can be rate-adjusted)?
  // If it doesn't, would we change the type to Invalid, or NonAdjustable?
  // Depending on how we handle the failure, we could defer the check until TuneForError is called.
}

// Set the PID coefficients for this clock.
void AudioClock::ConfigureAdjustment(const clock::PidControl::Coefficients& pid_coefficients) {
  rate_adjuster_ = clock::PidControl(pid_coefficients);
}

zx::clock AudioClock::DuplicateClock() const {
  if (!is_valid()) {
    return zx::clock();
  }

  auto result = audio::clock::DuplicateClock(clock_);
  FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.

  return result.take_value();
}

bool AudioClock::set_controls_tuneable_clock(bool controls_tuneable_clock) {
  if (is_device_clock() || is_flexible()) {
    controls_tuneable_clock = false;
  }
  controls_tuneable_clock_ = controls_tuneable_clock;
  return controls_tuneable_clock;
}

const TimelineFunction& AudioClock::ref_clock_to_clock_mono() {
  FX_CHECK(is_valid());

  auto result = audio::clock::SnapshotClock(clock_);
  FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.
  ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;

  return ref_clock_to_clock_mono_;
}

zx::time AudioClock::Read() const {
  FX_CHECK(is_valid());

  zx::time ref_now;
  auto status = clock_.read(ref_now.get_address());
  FX_CHECK(status == ZX_OK) << "Error while reading clock: " << status;

  return ref_now;
}

zx::time AudioClock::ReferenceTimeFromMonotonicTime(zx::time mono_time) const {
  FX_CHECK(is_valid());

  auto result = audio::clock::ReferenceTimeFromMonotonicTime(clock_, mono_time);
  FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.

  return result.take_value();
}

zx::time AudioClock::MonotonicTimeFromReferenceTime(zx::time ref_time) const {
  FX_CHECK(is_valid());

  auto result = audio::clock::MonotonicTimeFromReferenceTime(clock_, ref_time);
  FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.

  return result.take_value();
}

// The time units here are dest frames, the error factor is a difference in frac_src frames.
// This method is periodically called, to incorporate the current source position error (in
// fractional frames) into a feedback control that tunes the rate-adjustment factor for this clock.
void AudioClock::TuneRateForError(int64_t dest_frame, Fixed frac_src_error) {
  FX_CHECK(is_valid()) << "Invalid clock cannot be rate-adjusted";
  FX_CHECK(is_client_clock() || is_tuneable()) << "Cannot rate-adjust a static device clock";

  FX_LOGS(TRACE) << __func__ << "(" << frac_src_error.raw_value() << ", " << dest_frame
                 << ") for clock type "
                 << (type_ == Type::Adjustable
                         ? "Adjustable"
                         : (type_ == Type::NonAdjustable ? "Non-adjustable" : "Invalid"));

  // Feed into the PID
  rate_adjuster_.TuneForError(dest_frame, frac_src_error.raw_value());

  // This is a zero-centric adjustment factor, relative to current rate.
  auto adjustment = rate_adjuster_.Read();

  if (is_tuneable()) {
    // TODO(fxbug.dev/46648): adjust hardware clock rate by rate_adjustment
    // Return value from rate_adjuster_.Read is a double rate ratio centered on 1.0
    // Convert this to a (truncated) PPM value, set_rate_adjust and update the device clock
  } else if (is_flexible()) {
    // TODO(fxbug.dev/46651): adjust clock by rate_adjustment
    // Return value from rate_adjuster_.Read is a double rate ratio centered on 1.0
    // Convert this to a (truncated) PPM value, set_rate_adjust and update the client clock
  } else {
    // Else if custom: we've already absorbed the rate adjustment; it can be retrieved now.
    adjustment = std::clamp(adjustment, -0.002, 0.002);
    auto adjustment_rate = 1.0 + adjustment;
    rate_adjustment_ = TimelineRate(adjustment_rate);

    FX_LOGS(TRACE) << "Micro-SRC:" << std::fixed << std::setprecision(2) << std::setw(8)
                   << adjustment * 1'000'000.0 << " ppm; frac_error " << frac_src_error.raw_value();
  }
}

void AudioClock::ResetRateAdjustment(int64_t time) {
  rate_adjustment_ = TimelineRate(1u);
  rate_adjuster_.Start(time);
}

}  // namespace media::audio
