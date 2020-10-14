// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>

#include <cmath>
#include <iomanip>

#include "src/media/audio/audio_core/audio_clock_coefficients.h"
#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

//
// static methods
//
AudioClock AudioClock::CreateAsClientAdjustable(zx::clock clock) {
  return AudioClock(std::move(clock), Source::Client, true);
}

AudioClock AudioClock::CreateAsClientNonadjustable(zx::clock clock) {
  return AudioClock(std::move(clock), Source::Client, false);
}

AudioClock AudioClock::CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain) {
  return AudioClock(std::move(clock), Source::Device, true, domain);
}

AudioClock AudioClock::CreateAsDeviceNonadjustable(zx::clock clock, uint32_t domain) {
  return AudioClock(std::move(clock), Source::Device, false, domain);
}

//
// Policy-related static methods
Mixer::Resampler AudioClock::UpgradeResamplerIfNeeded(Mixer::Resampler initial_resampler_hint,
                                                      AudioClock& source_clock,
                                                      AudioClock& dest_clock) {
  if (initial_resampler_hint == Mixer::Resampler::Default) {
    auto mode = AudioClock::SynchronizationMode(source_clock, dest_clock);
    // If we might need micro-SRC for synchronization, use the higher quality resampler.
    if (mode == AudioClock::SyncMode::MicroSrc ||
        mode == AudioClock::SyncMode::AdjustSourceHardware ||
        mode == AudioClock::SyncMode::AdjustDestHardware) {
      return Mixer::Resampler::WindowedSinc;
    }
  }

  return initial_resampler_hint;
}

AudioClock::SyncMode AudioClock::SynchronizationMode(AudioClock& source_clock,
                                                     AudioClock& dest_clock) {
  if (source_clock == dest_clock) {
    return SyncMode::None;
  }

  if (source_clock.is_device_clock() && dest_clock.is_device_clock() &&
      source_clock.domain() == dest_clock.domain()) {
    return SyncMode::None;
  }

  if (source_clock.is_adjustable() && source_clock.is_client_clock()) {
    return SyncMode::AdjustSourceClock;
  }

  if (dest_clock.is_adjustable() && dest_clock.is_client_clock()) {
    return SyncMode::AdjustDestClock;
  }

  if (source_clock.is_adjustable() && dest_clock.controls_device_clock()) {
    return SyncMode::AdjustSourceHardware;
  }

  if (dest_clock.is_adjustable() && source_clock.controls_device_clock()) {
    return SyncMode::AdjustDestHardware;
  }

  return SyncMode::MicroSrc;
}

int32_t AudioClock::ClampPpm(SyncMode sync_mode, int32_t parts_per_million) {
  if (sync_mode == SyncMode::MicroSrc) {
    return std::clamp<int32_t>(parts_per_million, -kMicroSrcAdjustmentPpmMax,
                               kMicroSrcAdjustmentPpmMax);
  }

  return std::clamp<int32_t>(parts_per_million, ZX_CLOCK_UPDATE_MIN_RATE_ADJUST,
                             ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);
}

//
// Based on policy separately defined above, synchronize two clocks. Returns the ppm value of any
// micro-SRC that is needed. Error factor is a delta in frac_src frames, time units are dest frames.
int32_t AudioClock::SynchronizeClocks(AudioClock& source_clock, AudioClock& dest_clock,
                                      Fixed frac_src_error, int64_t dest_frame) {
  // The two clocks determine the sync mode.
  auto sync_mode = SynchronizationMode(source_clock, dest_clock);

  // From the sync mode, determine which clock to tune, and the appropriate PID.
  AudioClock* clock;
  audio::clock::PidControl* feedback_control;
  switch (sync_mode) {
    case SyncMode::None:
      // Same clock, or device clocks in the same clock domain. No need to adjust anything.
      return 0;

    case SyncMode::AdjustSourceClock:
    case SyncMode::AdjustSourceHardware:
      // We will adjust the source -- either its zx::clock, or the hardware it represents.
      clock = &source_clock;
      FX_CHECK(clock->adjustable_feedback_control_.has_value());

      feedback_control = &(clock->adjustable_feedback_control_.value());
      break;

    case SyncMode::AdjustDestClock:
    case SyncMode::AdjustDestHardware:
      // We will adjust the dest -- either its zx::clock, or the hardware it represents.
      clock = &dest_clock;
      FX_CHECK(clock->adjustable_feedback_control_.has_value());

      feedback_control = &(clock->adjustable_feedback_control_.value());
      break;

    case SyncMode::MicroSrc:
      // No clock is adjustable; use micro-SRC. Either can do the accounting; we choose source.
      clock = &source_clock;

      feedback_control = &clock->microsrc_feedback_control_;
      break;
  }
  feedback_control->TuneForError(dest_frame, frac_src_error.raw_value());

  // 'adjustment' is a zero-centric adjustment factor, relative to current rate.
  auto adjustment = feedback_control->Read();
  auto adjust_ppm = ClampPpm(sync_mode, round(adjustment * 1'000'000));

  if (adjust_ppm != clock->adjustment_ppm_) {
    clock->adjustment_ppm_ = adjust_ppm;
    FX_LOGS(TRACE) << "For sync_mode " << static_cast<uint32_t>(sync_mode) << ", adjusted to "
                   << std::setw(5) << adjust_ppm << " ppm; frac_err " << frac_src_error.raw_value();
  }

  return (sync_mode == SyncMode::MicroSrc ? adjust_ppm : 0);
}

//
// instance methods
//
AudioClock::AudioClock(zx::clock clock, Source source, bool adjustable, uint32_t domain)
    : clock_(std::move(clock)),
      source_(source),
      is_adjustable_(adjustable),
      domain_(domain),
      adjustment_ppm_(0),
      controls_device_clock_(false) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(clock_.get_handle(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                   nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "Failed to to fetch clock rights";

  const auto kRequiredRights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ |
                               (is_adjustable_ ? ZX_RIGHT_WRITE : 0);
  auto rights = info.rights & kRequiredRights;
  FX_CHECK(rights == kRequiredRights)
      << "Rights: actual 0x" << std::hex << rights << ", expected 0x" << kRequiredRights;

  // If we can read the clock now, we will always be able to. This check covers all error modes (bad
  // handle, wrong object type, no RIGHT_READ, clock not running) short of actually adjusting it.
  zx_time_t now_unused;
  if (clock_.read(&now_unused) != ZX_OK) {
    clock_.reset();
    FX_CHECK(false);
  }

  // Set feedback controls (including PID coefficients) for synchronizing this clock.
  if (is_adjustable()) {
    switch (source_) {
      case Source::Client:
        adjustable_feedback_control_ = audio::clock::PidControl(kPidFactorsAdjustClientClock);
        break;
      case Source::Device:
        adjustable_feedback_control_ = audio::clock::PidControl(kPidFactorsAdjustHardwareClock);
        break;
    }  // no default, to catch logic errors if an enum is added
  }
  microsrc_feedback_control_ = audio::clock::PidControl(kPidFactorsMicroSrc);
}

void AudioClock::set_controls_device_clock(bool should_control_device_clock) {
  if (is_client_clock() && !is_adjustable_) {
    controls_device_clock_ = should_control_device_clock;
  }
}

// We pre-qualify the clock, so the following methods should never fail.
TimelineFunction AudioClock::ref_clock_to_clock_mono() const {
  return audio::clock::SnapshotClock(clock_).take_value().reference_to_monotonic;
}

zx::time AudioClock::ReferenceTimeFromMonotonicTime(zx::time mono_time) const {
  return audio::clock::ReferenceTimeFromMonotonicTime(clock_, mono_time).take_value();
}

zx::time AudioClock::MonotonicTimeFromReferenceTime(zx::time ref_time) const {
  return audio::clock::MonotonicTimeFromReferenceTime(clock_, ref_time).take_value();
}

zx::clock AudioClock::DuplicateClock() const {
  return audio::clock::DuplicateClock(clock_).take_value();
}

zx::time AudioClock::Read() const {
  zx::time ref_now;
  clock_.read(ref_now.get_address());

  return ref_now;
}

void AudioClock::ResetRateAdjustment(int64_t time) {
  microsrc_feedback_control_.Start(time);
  if (adjustable_feedback_control_.has_value()) {
    adjustable_feedback_control_.value().Start(time);
  }
}

}  // namespace media::audio
