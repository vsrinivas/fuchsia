// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>

#include "src/media/audio/lib/clock/pid_control.h"

namespace media::audio {

// These values were determined empirically based on one accepted rule-of-thumb for setting PID
// factors. First discover the P factor (without I or D factors) that leads to steady-state
// oscillation, then half that value. Set the I factor to approximately (2P)/OscillationPeriod.
// Set the D factor to approximately (P/8)*OscillationPeriod.
constexpr double kMicroSrcTypicalOscillationPeriod = 6000;  // 480
                                                            // (frames in typical oscillation cycle)
constexpr double kMicroSrcPFactor = -0.0000000312;
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

  // Next: set micro-SRC PID coefficients approprately for a software-tuned clock.
  audio_clock.ConfigureAdjustment(kPidFactorsMicroSrc);

  return audio_clock;
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

bool AudioClock::SetAsHardwareControlling(bool controls_hardware_clock) {
  if (is_device_clock() || is_adjustable()) {
    controls_hardware_clock = false;
  }
  controls_hardware_clock_ = controls_hardware_clock;
  return controls_hardware_clock;
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
void AudioClock::TuneRateForError(Fixed frac_src_error, int64_t dest_frame) {
  FX_CHECK(type_ != Type::Invalid) << "Invalid clock cannot be rate-adjusted";
  FX_CHECK((type_ == Type::Adjustable) || (source_ == Source::Client))
      << "Cannot rate-adjust a static device clock";

  FX_LOGS(TRACE) << __func__ << "(" << frac_src_error.raw_value() << ", " << dest_frame
                 << ") for clock type "
                 << (type_ == Type::Adjustable
                         ? "Adjustable"
                         : (type_ == Type::NonAdjustable ? "Non-adjustable" : "Invalid"));

  // Feed into the PID
  rate_adjuster_.TuneForError(dest_frame, frac_src_error.raw_value());

  // This is a zero-centric adjustment factor, relative to current rate.
  auto adjustment = rate_adjuster_.Read();

  if (source_ == Source::Device) {
    // TODO(fxbug.dev/46648): adjust hardware clock rate by rate_adjustment
    // Return value from rate_adjuster_.Read is a double rate ratio centered on 1.0
    // Convert this to a (truncated) PPM value, set_rate_adjust and update the device clock
  } else if (type_ == Type::Adjustable) {
    // TODO(fxbug.dev/46651): adjust clock by rate_adjustment
    // Return value from rate_adjuster_.Read is a double rate ratio centered on 1.0
    // Convert this to a (truncated) PPM value, set_rate_adjust and update the client clock
  } else {
    // Else if custom: we've already absorbed the rate adjustment; it can be retrieved now.
    auto adjustment_rate = 1.0 + adjustment;
    rate_adjustment_ = TimelineRate(static_cast<float>(adjustment_rate));

    FX_LOGS(TRACE) << "Micro-SRC adjustment: adjustment " << adjustment << ", adjustment_rate "
                   << adjustment_rate << " (" << rate_adjustment_.subject_delta() << "/"
                   << rate_adjustment_.reference_delta() << "), error "
                   << frac_src_error.raw_value();
  }
}

void AudioClock::ResetRateAdjustmentTuning(int64_t time) {
  rate_adjustment_ = TimelineRate(1u);
  rate_adjuster_.Start(time);
}

}  // namespace media::audio
