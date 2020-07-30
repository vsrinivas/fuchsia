// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>

namespace media::audio {

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
  // Depending on how we handle such a failure, we could defer the check until RateAdjust is called.
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

zx::clock AudioClock::DuplicateClock() const {
  if (!is_valid()) {
    return zx::clock();
  }

  auto result = audio::clock::DuplicateClock(clock_);
  FX_CHECK(result.is_ok());  // We pre-qualify the clock, so this should never fail.

  return result.take_value();
}

}  // namespace media::audio
