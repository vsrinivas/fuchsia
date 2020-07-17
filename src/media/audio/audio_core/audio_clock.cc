// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

namespace media::audio {

// static methods
AudioClock AudioClock::CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, Type::Adjustable, domain);

  // Set HW-response PID coefficients approprately for a tuned hardware clock
  return audio_clock;
}

AudioClock AudioClock::CreateAsDeviceStatic(zx::clock clock, uint32_t domain) {
  auto audio_clock = AudioClock(std::move(clock), Source::Device, Type::NonAdjustable, domain);

  return audio_clock;
}

AudioClock AudioClock::CreateAsOptimal(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, Type::Adjustable);

  // Set HW-chaser PID coefficients approprately for an optimal clock
  return audio_clock;
}

AudioClock AudioClock::CreateAsCustom(zx::clock clock) {
  auto audio_clock = AudioClock(std::move(clock), Source::Client, Type::NonAdjustable);

  // Set micro-SRC PID coefficients approprately for a software-tuned clock
  return audio_clock;
}

AudioClock::AudioClock(zx::clock clock, Source source, Type type, uint32_t domain)
    : clock_(std::move(clock)), source_(source), type_(type), domain_(domain) {
  if (!clock_.is_valid()) {
    type_ = Type::Invalid;
  }

  if (is_valid()) {
    auto result = audio::clock::SnapshotClock(clock_);
    if (result.is_ok()) {
      ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;
    } else {
      FX_LOGS(WARNING) << "SnapshotClock failed; marking clock as invalid";
      type_ = Type::Invalid;
    }
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
  FX_DCHECK(clock_.is_valid()) << "this (" << std::hex << static_cast<void*>(this)
                               << "), ref_clock_to_clock_mono called before clock was valid";

  auto result = audio::clock::SnapshotClock(clock_);
  FX_DCHECK(result.is_ok()) << "SnapshotClock failed";
  ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;

  return ref_clock_to_clock_mono_;
}

zx::time AudioClock::Read() const {
  FX_DCHECK(clock_.is_valid()) << "Invalid clock cannot be read";
  zx::time now;
  auto status = clock_.read(now.get_address());
  FX_DCHECK(status == ZX_OK) << "Error while reading clock: " << status;
  return now;
}

fit::result<zx::time, zx_status_t> AudioClock::ReferenceTimeFromMonotonicTime(
    zx::time mono_time) const {
  if (!is_valid()) {
    return fit::error(ZX_ERR_BAD_HANDLE);
  }
  return audio::clock::ReferenceTimeFromMonotonicTime(clock_, mono_time);
}

fit::result<zx::time, zx_status_t> AudioClock::MonotonicTimeFromReferenceTime(
    zx::time ref_time) const {
  if (!is_valid()) {
    return fit::error(ZX_ERR_BAD_HANDLE);
  }
  return audio::clock::MonotonicTimeFromReferenceTime(clock_, ref_time);
}

fit::result<zx::clock, zx_status_t> AudioClock::DuplicateClock() const {
  if (!is_valid()) {
    return fit::error(ZX_ERR_BAD_HANDLE);
  }
  return audio::clock::DuplicateClock(clock_);
}

}  // namespace media::audio
