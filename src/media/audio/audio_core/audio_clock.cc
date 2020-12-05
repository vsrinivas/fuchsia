// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>

#include <cmath>
#include <iomanip>
#include <string>

#include "src/media/audio/audio_core/audio_clock_coefficients.h"
#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

//
// static methods
//
AudioClock AudioClock::ClientAdjustable(zx::clock clock) {
  return AudioClock(std::move(clock), Source::Client, true);
}

AudioClock AudioClock::ClientFixed(zx::clock clock) {
  return AudioClock(std::move(clock), Source::Client, false);
}

AudioClock AudioClock::DeviceAdjustable(zx::clock clock, uint32_t domain) {
  return AudioClock(std::move(clock), Source::Device, true, domain);
}

AudioClock AudioClock::DeviceFixed(zx::clock clock, uint32_t domain) {
  return AudioClock(std::move(clock), Source::Device, false, domain);
}

//
// Policy-related static methods
Mixer::Resampler AudioClock::UpgradeResamplerIfNeeded(Mixer::Resampler initial_resampler_hint,
                                                      AudioClock& source_clock,
                                                      AudioClock& dest_clock) {
  // If we use micro-SRC for synchronization, select the higher quality resampler.
  if (initial_resampler_hint == Mixer::Resampler::Default &&
      AudioClock::SyncModeForClocks(source_clock, dest_clock) == AudioClock::SyncMode::MicroSrc) {
    return Mixer::Resampler::WindowedSinc;
  }

  return initial_resampler_hint;
}

AudioClock::SyncMode AudioClock::SyncModeForClocks(AudioClock& source_clock,
                                                   AudioClock& dest_clock) {
  if (source_clock == dest_clock) {
    return SyncMode::None;
  }

  if (source_clock.is_device_clock() && dest_clock.is_device_clock() &&
      source_clock.domain() == dest_clock.domain()) {
    return SyncMode::None;
  }

  // If device clock is in MONOTONIC domain, ClientAdjustable (which prior to rate-adjustment runs
  // at the monotonic rate) need not be adjusted -- so no sync is required.
  if ((source_clock.is_client_clock() && source_clock.is_adjustable()) &&
      (dest_clock.is_device_clock() && dest_clock.domain() == kMonotonicDomain)) {
    return SyncMode::ResetSourceClock;
  }

  if ((dest_clock.is_client_clock() && dest_clock.is_adjustable()) &&
      (source_clock.is_device_clock() && source_clock.domain() == kMonotonicDomain)) {
    return SyncMode::ResetDestClock;
  }

  // Otherwise, a client adjustable clock should be adjusted
  if (source_clock.is_adjustable() && source_clock.is_client_clock()) {
    return SyncMode::AdjustSourceClock;
  }

  if (dest_clock.is_adjustable() && dest_clock.is_client_clock()) {
    return SyncMode::AdjustDestClock;
  }

  return SyncMode::MicroSrc;
}

// Based on policy separately defined above, synchronize two clocks. Returns the ppm value of any
// micro-SRC that is needed. Error factor is a delta in frac_src frames, time is dest ref time.
int32_t AudioClock::SynchronizeClocks(AudioClock& source_clock, AudioClock& dest_clock,
                                      zx::time monotonic_time, zx::duration src_pos_error) {
  // The two clocks determine the sync mode.
  // From the sync mode, determine which clock to tune, and the appropriate PID.
  switch (SyncModeForClocks(source_clock, dest_clock)) {
    case SyncMode::None:
      // Same clock, or device clocks in same domain. No need to adjust anything (or micro-SRC).
      return 0;

    case SyncMode::ResetSourceClock:
      // Immediately return the source clock to a monotonic rate, if it isn't already.
      // TODO(fxbug.dev/64169): Converge position error to 0 before resetting to monotonic rate.
      // Position error is guaranteed to be within our threshold; converging to 0 would be ideal.
      source_clock.AdjustClock(0);
      source_clock.ResetRateAdjustment(monotonic_time);
      return 0;

    case SyncMode::ResetDestClock:
      // Immediately return the dest clock to a monotonic rate, if it isn't already.
      // TODO(fxbug.dev/64169): Converge position error to 0 before resetting to monotonic rate.
      // Position error is guaranteed to be within our threshold; converging to 0 would be ideal.
      dest_clock.AdjustClock(0);
      dest_clock.ResetRateAdjustment(monotonic_time);
      return 0;

    case SyncMode::AdjustSourceClock:
      // Adjust the source's zx::clock. No micro-SRC needed.
      source_clock.TuneForError(monotonic_time, src_pos_error);
      return 0;

    case SyncMode::AdjustDestClock:
      // Adjust the dest's zx::clock. No micro-SRC needed.
      dest_clock.TuneForError(monotonic_time, zx::duration(0) - src_pos_error);
      return 0;

    case SyncMode::MicroSrc:
      // No clock is adjustable; use micro-SRC (tracked by the client-side clock object).
      AudioClock* client_clock;
      if (source_clock.is_client_clock()) {
        client_clock = &source_clock;
      } else {
        // Although the design doesn't strictly require it, this CHECK (and other assumptions in
        // AudioClock or MixStage) require is_client_clock() for one of the two clocks.
        FX_CHECK(dest_clock.is_client_clock());
        client_clock = &dest_clock;
      }
      return client_clock->TuneForError(monotonic_time, src_pos_error);
  }
}

std::string AudioClock::SyncModeToString(SyncMode mode) {
  switch (mode) {
    case SyncMode::None:
      // Same clock, or device clocks in same domain. No need to adjust anything (or micro-SRC).
      return "'None'";

      // Return the clock to monotonic rate if it isn't already, and stop checking for divergence.
    case SyncMode::ResetSourceClock:
      return "'Sync Source to match MONOTONIC Dest'";
    case SyncMode::ResetDestClock:
      return "'Sync Dest to match MONOTONIC Source'";

      // Adjust the clock's underlying zx::clock. No micro-SRC needed.
    case SyncMode::AdjustSourceClock:
      return "'Adjust Source to match non-MONOTONIC Dest'";
    case SyncMode::AdjustDestClock:
      return "'Adjust Dest to match non-MONOTONIC Source'";

      // No clock is adjustable; use micro-SRC (tracked by the client-side clock object).
    case SyncMode::MicroSrc:
      return "'Micro-SRC'";
  }
  // No default: clause, so newly-added enums get caught and added here.
}

void AudioClock::DisplaySyncInfo(AudioClock& source_clock, AudioClock& dest_clock) {
  auto sync_mode = SyncModeForClocks(source_clock, dest_clock);

  auto mono_to_source_ref = source_clock.ref_clock_to_clock_mono().Inverse();
  double source_ppm =
      1'000'000.0 * mono_to_source_ref.subject_delta() / mono_to_source_ref.reference_delta() -
      1'000'000.0;

  auto mono_to_dest_ref = dest_clock.ref_clock_to_clock_mono().Inverse();
  double dest_ppm =
      1'000'000.0 * mono_to_dest_ref.subject_delta() / mono_to_dest_ref.reference_delta() -
      1'000'000.0;

  std::string micro_src_str;
  if (sync_mode == SyncMode::MicroSrc) {
    auto micro_src_ppm =
        (source_clock.is_client_clock() ? source_clock : dest_clock).previous_adjustment_ppm_;
    micro_src_str += " Latest micro-src " + std::to_string(micro_src_ppm) + " ppm.";
  }

  FX_LOGS(INFO) << "Sync mode " << SyncModeToString(sync_mode) << " ("
                << static_cast<size_t>(sync_mode) <<
      // sync_mode <<
      //
      "). Source (" << (source_clock.is_client_clock() ? "client" : "device") << ") " << source_ppm
                << " ppm. Dest (" << (dest_clock.is_client_clock() ? "client" : "device") << ") "
                << dest_ppm << " ppm." << micro_src_str;
}

//
// instance methods
//
AudioClock::AudioClock(zx::clock clock, Source source, bool adjustable, uint32_t domain)
    : clock_(std::move(clock)), source_(source), is_adjustable_(adjustable), domain_(domain) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(clock_.get_handle(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                   nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "Failed to to fetch clock rights";

  const auto kRequiredRights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ |
                               (is_adjustable_ ? ZX_RIGHT_WRITE : 0);
  auto rights = info.rights & kRequiredRights;
  FX_CHECK(rights == kRequiredRights)
      << "Rights: actual 0x" << std::hex << rights << ", expected 0x" << kRequiredRights;

  // If we can read the clock now, we will always be able to. This check covers all error modes
  // except actual adjustment (bad handle, wrong object type, no RIGHT_READ, clock not running).
  zx_time_t now_unused;
  FX_CHECK(clock_.read(&now_unused) == ZX_OK) << "Submitted zx::clock could not be read";

  // Set feedback controls (including PID coefficients) for synchronizing this clock.
  if (is_adjustable()) {
    switch (source_) {
      case Source::Client:
        feedback_control_ = audio::clock::PidControl(kPidFactorsAdjustClientClock);
        break;
      case Source::Device:
        feedback_control_ = audio::clock::PidControl(kPidFactorsAdjustDeviceClock);
        break;
    }  // no default, to catch logic errors if an enum is added
  } else {
    feedback_control_ = audio::clock::PidControl(kPidFactorsMicroSrc);
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

int32_t AudioClock::ClampPpm(int32_t parts_per_million) {
  if (!is_adjustable() && is_client_clock()) {
    return std::clamp<int32_t>(parts_per_million, -kMicroSrcAdjustmentPpmMax,
                               kMicroSrcAdjustmentPpmMax);
  }

  return std::clamp<int32_t>(parts_per_million, ZX_CLOCK_UPDATE_MIN_RATE_ADJUST,
                             ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);
}

void AudioClock::ResetRateAdjustment(zx::time reset_time) { feedback_control_.Start(reset_time); }

int32_t AudioClock::TuneForError(zx::time monotonic_time, zx::duration src_pos_error) {
  // Tune the PID and retrieve the current correction (a zero-centric, rate-relative adjustment).
  feedback_control_.TuneForError(monotonic_time, src_pos_error.get());
  double rate_adjustment = feedback_control_.Read();
  int32_t rate_adjust_ppm = ClampPpm(round(rate_adjustment * 1'000'000.0));

  if (rate_adjust_ppm != previous_adjustment_ppm_) {
    FX_LOGS(DEBUG) << static_cast<void*>(this) << (is_client_clock() ? " Client" : " Device")
                   << (is_adjustable() ? "Adjustable" : "Fixed") << " clock changed from (ppm) "
                   << std::setw(5) << previous_adjustment_ppm_ << " to " << std::setw(5)
                   << rate_adjust_ppm << "; src_pos_err " << std::setw(6) << src_pos_error.get();

  } else {
    FX_LOGS(TRACE) << static_cast<void*>(this) << (is_client_clock() ? " Client" : " Device")
                   << (is_adjustable() ? "Adjustable" : "Fixed") << " adjust_ppm remains (ppm) "
                   << std::setw(5) << previous_adjustment_ppm_ << " for the src pos error "
                   << std::setw(6) << src_pos_error.get();
  }

  AdjustClock(rate_adjust_ppm);
  return rate_adjust_ppm;
}

void AudioClock::AdjustClock(int32_t rate_adjust_ppm) {
  if (previous_adjustment_ppm_ == rate_adjust_ppm) {
    return;
  }

  // If this is an actual clock, adjust it; else just cache rate_adjust_ppm for micro-SRC.
  if (is_adjustable()) {
    zx::clock::update_args args;
    args.reset().set_rate_adjust(rate_adjust_ppm);
    FX_CHECK(clock_.update(args) == ZX_OK) << "Adjustable clock could not be rate-adjusted";
  }

  previous_adjustment_ppm_ = rate_adjust_ppm;
}

}  // namespace media::audio
