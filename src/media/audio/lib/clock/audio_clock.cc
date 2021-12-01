// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/audio_clock.h"

#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "src/media/audio/lib/clock/audio_clock_coefficients.h"
#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

// Enable logging of sync-related clock adjustments.
constexpr bool kLogClockTuning = true;
// (if enabled) log all source position errors that are equal to or greater than this threshold.
constexpr zx::duration kLogClockTuningPositionErrorThreshold = zx::nsec(500);
// (if enabled) otherwise, log the first of every ~100 errors (in practice this is approx 1/sec).
constexpr int64_t kClockTuneLoggingStride = 97;  // make strides prime, to avoid periodicity

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
bool AudioClock::NoSynchronizationRequired(AudioClock& source_clock, AudioClock& dest_clock) {
  return AudioClock::SyncModeForClocks(source_clock, dest_clock) == AudioClock::SyncMode::None;
}

// We know we need a high-quality resampler if the clocks indicate AudioClock::SyncMode::MicroSrc.
// Even for AudioClock::SyncMode::None, we might still need micro-SRC in the future if either clock
// is client-rate-adjustable and the two clocks are not in fact the SAME entity.
bool AudioClock::SynchronizationNeedsHighQualityResampler(AudioClock& source_clock,
                                                          AudioClock& dest_clock) {
  auto sync_mode = SyncModeForClocks(source_clock, dest_clock);
  if (sync_mode == SyncMode::MicroSrc) {
    return true;
  }
  if (sync_mode != SyncMode::None || source_clock == dest_clock) {
    return false;
  }
  return ((source_clock.is_client_clock() && !source_clock.is_adjustable()) ||
          (dest_clock.is_client_clock() && !dest_clock.is_adjustable()));
}

AudioClock::SyncMode AudioClock::SyncModeForClocks(AudioClock& source_clock,
                                                   AudioClock& dest_clock) {
  // Compare koids of the underlying zx::clocks
  if (source_clock == dest_clock) {
    return SyncMode::None;
  }

  if (source_clock.is_device_clock() && dest_clock.is_device_clock() &&
      source_clock.domain() == dest_clock.domain()) {
    return SyncMode::None;
  }

  if (source_clock.is_clock_monotonic() && dest_clock.is_clock_monotonic()) {
    return SyncMode::None;
  }

  // If device clock is in MONOTONIC domain, ClientAdjustable (which prior to rate-adjustment runs
  // at the monotonic rate) need not be adjusted -- so no sync is required.
  if ((source_clock.is_client_clock() && source_clock.is_adjustable()) &&
      (dest_clock.is_device_clock() && dest_clock.domain() == kMonotonicDomain)) {
    return SyncMode::RevertSourceToMonotonic;
  }

  if ((dest_clock.is_client_clock() && dest_clock.is_adjustable()) &&
      (source_clock.is_device_clock() && source_clock.domain() == kMonotonicDomain)) {
    return SyncMode::RevertDestToMonotonic;
  }

  // Otherwise, a client adjustable clock should be adjusted
  if (source_clock.is_client_clock() && source_clock.is_adjustable()) {
    return SyncMode::AdjustSourceClock;
  }

  if (dest_clock.is_client_clock() && dest_clock.is_adjustable()) {
    return SyncMode::AdjustDestClock;
  }

  return SyncMode::MicroSrc;
}

void AudioClock::ResetRateAdjustments(AudioClock& source_clock, AudioClock& dest_clock,
                                      zx::time reset_time) {
  auto sync_mode = SyncModeForClocks(source_clock, dest_clock);
  if (sync_mode == SyncMode::AdjustSourceClock) {
    source_clock.ResetRateAdjustment(reset_time);
  }
  if (sync_mode == SyncMode::AdjustDestClock) {
    dest_clock.ResetRateAdjustment(reset_time);
  }
  if (sync_mode == SyncMode::MicroSrc) {
    auto& client_clock = source_clock.is_client_clock() ? source_clock : dest_clock;
    client_clock.ResetRateAdjustment(reset_time);
  }
}

// Based on policy separately defined above, synchronize two clocks. Returns the ppm value of any
// micro-SRC that is needed. Error factor is a delta in frac_source frames, time is dest ref time.
int32_t AudioClock::SynchronizeClocks(AudioClock& source_clock, AudioClock& dest_clock,
                                      zx::time monotonic_time, zx::duration source_pos_error) {
  AudioClock* clock_to_adjust = &source_clock;
  int32_t adjust_ppm;

  // The two clocks determine sync mode, from which we know the clock and appropriate PID to tune.
  switch (SyncModeForClocks(source_clock, dest_clock)) {
    // Same clock, or device clocks in same domain. No need to adjust anything (or micro-SRC).
    case SyncMode::None:
      return 0;

    // Converge position proportionally instead of the normal mechanism. Doing this rather than
    // allowing the PID to fully settle (and then locking to 0) gets us to tight sync faster.
    case SyncMode::RevertDestToMonotonic:
      source_pos_error = zx::nsec(0) - source_pos_error;
      clock_to_adjust = &dest_clock;
      __FALLTHROUGH;
    case SyncMode::RevertSourceToMonotonic:
      adjust_ppm = static_cast<int32_t>(source_pos_error / kLockToMonotonicErrorThreshold);
      adjust_ppm = std::clamp<int32_t>(adjust_ppm, ZX_CLOCK_UPDATE_MIN_RATE_ADJUST,
                                       ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);

      if (clock_to_adjust->AdjustClock(adjust_ppm) != 0 && adjust_ppm == 0) {
        // If we set it to 0 ppm and it wasn't already, release any accumulated PID presure.
        clock_to_adjust->ResetRateAdjustment(monotonic_time);
      }
      return 0;

    // Tune the clock from its PID feedback control. No micro-SRC needed.
    case SyncMode::AdjustDestClock:
      source_pos_error = zx::nsec(0) - source_pos_error;
      clock_to_adjust = &dest_clock;
      __FALLTHROUGH;
    case SyncMode::AdjustSourceClock:
      clock_to_adjust->TuneForError(monotonic_time, source_pos_error);
      return 0;

    // No clock is adjustable; use micro-SRC (tracked by the client-side clock object).
    case SyncMode::MicroSrc:
      // Although the design doesn't strictly require it, these lines (and other assumptions in
      // AudioClock and MixStage) require "is_client_clock()==true" for one of the two clocks.
      if (!source_clock.is_client_clock()) {
        clock_to_adjust = &dest_clock;
        FX_CHECK(dest_clock.is_client_clock());
      }
      return clock_to_adjust->TuneForError(monotonic_time, source_pos_error);
  }
}

std::string AudioClock::SyncModeToString(SyncMode mode) {
  switch (mode) {
    case SyncMode::None:
      // Same clock, or device clocks in same domain. No need to adjust anything (or micro-SRC).
      return "'None'";

      // Return the clock to monotonic rate if it isn't already, and stop checking for divergence.
    case SyncMode::RevertSourceToMonotonic:
      return "'Match Source to MONOTONIC Dest'";
    case SyncMode::RevertDestToMonotonic:
      return "'Match Dest to MONOTONIC Source'";

      // Adjust the clock's underlying zx::clock. No micro-SRC needed.
    case SyncMode::AdjustSourceClock:
      return "'Adjust Source to match non-MONOTONIC Dest'";
    case SyncMode::AdjustDestClock:
      return "'Adjust Dest to match non-MONOTONIC Source'";

      // No clock is adjustable; use micro-SRC (tracked by the client-side clock object).
    case SyncMode::MicroSrc:
      return "'Micro-SRC'";

      // No default clause, so newly-added enums get caught and added here.
  }
}

std::string AudioClock::SyncInfo(AudioClock& source_clock, AudioClock& dest_clock) {
  auto sync_mode = SyncModeForClocks(source_clock, dest_clock);

  auto mono_to_source_ref = source_clock.ref_clock_to_clock_mono().Inverse();
  double mono_to_source_rate = static_cast<double>(mono_to_source_ref.subject_delta()) /
                               static_cast<double>(mono_to_source_ref.reference_delta());
  double source_ppm = 1'000'000.0 * mono_to_source_rate - 1'000'000.0;

  auto mono_to_dest_ref = dest_clock.ref_clock_to_clock_mono().Inverse();
  double mono_to_dest_rate = static_cast<double>(mono_to_dest_ref.subject_delta()) /
                             static_cast<double>(mono_to_dest_ref.reference_delta());
  double dest_ppm = 1'000'000.0 * mono_to_dest_rate - 1'000'000.0;

  std::string micro_src_str;
  if (sync_mode == SyncMode::MicroSrc) {
    auto micro_src_ppm =
        (source_clock.is_client_clock() ? source_clock : dest_clock).current_adjustment_ppm_;
    micro_src_str += " Latest micro-src " + std::to_string(micro_src_ppm) + " ppm.";
  }

  std::stringstream sync_stream;
  sync_stream << "Mode " << SyncModeToString(sync_mode) << " (" << static_cast<size_t>(sync_mode)
              << "). Source (" << (source_clock.is_client_clock() ? "cli" : "dev") << ") "
              << source_ppm << " ppm. Dest (" << (dest_clock.is_client_clock() ? "cli" : "dev")
              << ") " << dest_ppm << " ppm." << micro_src_str;
  return sync_stream.str();
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
  zx_clock_details_v1_t details;
  switch (source_) {
    case Source::Client:
      // A client clock is always adjustable by its owner, so we make the following judgment call:
      // ** Once a clock is rate-adjusted, we will never subsequently query whether it is identical
      // to CLOCK_MONOTONIC. **  I.e. at that point we will no longer bother to check the details.
      // For an Adjustable clock, we own it and thus can deterministically toggle is_monotonic_
      // (one-way, from TRUE to FALSE) when we rate-adjust it for the first time. For a Fixed clock,
      // the client (or some other party) owns it, so initially we check its rate and offset and
      // generation_counter, so that subsequently we only need check its generation_counter.
      status = clock_.get_details(&details);
      is_clock_monotonic_ = (status == ZX_OK) && (details.generation_counter == 0) &&
                            (details.mono_to_synthetic.reference_offset ==
                             details.mono_to_synthetic.synthetic_offset) &&
                            (details.mono_to_synthetic.rate.reference_ticks ==
                             details.mono_to_synthetic.rate.synthetic_ticks);

      feedback_control_ = is_adjustable() ? audio::clock::PidControl(kPidFactorsAdjustClientClock)
                                          : audio::clock::PidControl(kPidFactorsMicroSrc);
      break;
    case Source::Device:
      // For a device clock, the clock domain tells us with certainty and finality whether it is
      // permanently locked to CLOCK_MONOTONIC
      is_clock_monotonic_ = (domain_ == kMonotonicDomain);

      feedback_control_ = is_adjustable() ? audio::clock::PidControl(kPidFactorsAdjustDeviceClock)
                                          : audio::clock::PidControl(kPidFactorsMicroSrc);
      break;
  }  // no default, to catch logic errors if an enum is added
}

bool AudioClock::is_clock_monotonic() {
  // If this is a client's custom clock and was clock_monotonic up until now, then double-check it,
  // but once we detect it is non-monotonic then the answer will be no forever.
  if (is_client_clock() && !is_adjustable_ && is_clock_monotonic_) {
    zx_clock_details_v1_t details;
    auto status = clock_.get_details(&details);
    is_clock_monotonic_ = (status == ZX_OK) && (details.generation_counter == 0);
  }

  return is_clock_monotonic_;
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

fpromise::result<zx::clock, zx_status_t> AudioClock::DuplicateClock(zx_rights_t rights) const {
  zx::clock dup_clock;
  auto status = clock_.duplicate(rights, &dup_clock);
  if (status != ZX_OK) {
    return fpromise::error(status);
  }
  return fpromise::ok(std::move(dup_clock));
}

fpromise::result<zx::clock, zx_status_t> AudioClock::DuplicateClockReadOnly() const {
  constexpr auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
  return DuplicateClock(rights);
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

int32_t AudioClock::TuneForError(zx::time monotonic_time, zx::duration source_pos_error) {
  zx::duration src_pos_err_for_tune = source_pos_error;

  // Tune the PID and retrieve the current correction (a zero-centric, rate-relative adjustment).
  feedback_control_.TuneForError(monotonic_time,
                                 static_cast<double>(src_pos_err_for_tune.to_nsecs()));
  double rate_adjustment = feedback_control_.Read();
  int32_t rate_adjust_ppm = ClampPpm(
      static_cast<int32_t>(std::round(static_cast<double>(rate_adjustment) * 1'000'000.0)));

  LogClockAdjustments(source_pos_error, rate_adjust_ppm);

  AdjustClock(rate_adjust_ppm);

  return rate_adjust_ppm;
}

// If kLogClockTuning is enabled, then log if:
//    source position error is kLogClockTuningPositionErrorThreshold or more, or
//    it's been kClockTuneLoggingStride times since we last logged.
void AudioClock::LogClockAdjustments(zx::duration source_pos_error, int32_t rate_adjust_ppm) {
  if constexpr (kLogClockTuning) {
    static int64_t log_count = 0;

    // If absolute error is large enough, then log now but reset our stride.
    if (std::abs(source_pos_error.to_nsecs()) >= kLogClockTuningPositionErrorThreshold.to_nsecs()) {
      log_count = 0;
    }
    if (log_count == 0) {
      if (rate_adjust_ppm != current_adjustment_ppm_) {
        FX_LOGS(INFO) << static_cast<void*>(this) << (is_client_clock() ? " Client" : " Device")
                      << (is_adjustable() ? "Adjustable" : "Fixed     ") << " change from (ppm) "
                      << std::setw(4) << current_adjustment_ppm_ << " to " << std::setw(4)
                      << rate_adjust_ppm << "; src_pos_err " << std::setw(7)
                      << source_pos_error.to_nsecs() << " ns";
      } else {
        FX_LOGS(INFO) << static_cast<void*>(this) << (is_client_clock() ? " Client" : " Device")
                      << (is_adjustable() ? "Adjustable" : "Fixed     ")
                      << " adjust_ppm remains  (ppm) " << std::setw(4) << current_adjustment_ppm_
                      << "; src_pos_err " << std::setw(7) << source_pos_error.to_nsecs() << " ns";
      }
    }
    log_count = (++log_count) % kClockTuneLoggingStride;
  }
}

int32_t AudioClock::AdjustClock(int32_t rate_adjust_ppm) {
  auto previous_adjustment_ppm = current_adjustment_ppm_;
  if (current_adjustment_ppm_ != rate_adjust_ppm) {
    current_adjustment_ppm_ = rate_adjust_ppm;

    // If this is an actual clock, adjust it; else just cache rate_adjust_ppm for micro-SRC.
    if (is_adjustable()) {
      UpdateClockRate(rate_adjust_ppm);
    }
  }

  return previous_adjustment_ppm;
}

void AudioClock::UpdateClockRate(int32_t rate_adjust_ppm) {
  if (rate_adjust_ppm) {
    is_clock_monotonic_ = false;
  }

  zx::clock::update_args args;
  args.reset().set_rate_adjust(rate_adjust_ppm);
  FX_CHECK(clock_.update(args) == ZX_OK) << "Adjustable clock could not be rate-adjusted";
}

}  // namespace media::audio
