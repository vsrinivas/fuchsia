// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clock_synchronizer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/media/audio/lib/clock/audio_clock_coefficients.h"
#include "src/media/audio/lib/clock/logging.h"

namespace media_audio {

namespace {
constexpr int32_t kMicroSrcAdjustmentPpmMax = 2500;

// When tuning a ClientAdjustable to a monotonic target, we use proportional clock adjustments
// instead of the normal PID feedback control, because once a ClientAdjustable is aligned with its
// monotonic target, it stays aligned (the whole clock domain drifts together, if at all).
//
// We synchronize clocks as tightly as possible, in all sync modes; the 10-nsec error threshold
// below is the smallest possible threshold. Clock-tuning precision is limited to integer ppms. If
// 10 msec elapse between clock-sync measurements, a minimum rate adjustment (+/- 1ppm) will
// change the position error (relative to monotonic) by 10 nsec. So once position error is less
// than this threshold, we "lock" the client clock to 0 ppm.
//
// Note: this approach might yield acceptable results for synchronizing software clocks to
// non-monotonic targets as well. Further investigation/measurement is needed.
constexpr zx::duration kLockToMonotonicErrorThreshold = zx::nsec(10);
}  // namespace

// static
std::shared_ptr<ClockSynchronizer> ClockSynchronizer::Create(std::shared_ptr<Clock> leader,
                                                             std::shared_ptr<Clock> follower,
                                                             Mode mode) {
  FX_CHECK(leader);
  FX_CHECK(follower);

  // If we will adjust the follower clock's rate, the follower must be an adjustable clock.
  if (mode == Mode::WithAdjustments) {
    FX_CHECK(follower->adjustable());
  }

  struct MakePublicCtor : ClockSynchronizer {
    MakePublicCtor(std::shared_ptr<Clock> leader, std::shared_ptr<Clock> follower, Mode mode)
        : ClockSynchronizer(std::move(leader), std::move(follower), mode,
                            (mode == Mode::WithAdjustments)
                                ? media::audio::kPidFactorsClockChasesClock
                                : media::audio::kPidFactorsMicroSrc) {}
  };

  return std::make_shared<MakePublicCtor>(std::move(leader), std::move(follower), mode);
}

// static
std::shared_ptr<ClockSynchronizer> ClockSynchronizer::SelectModeAndCreate(
    std::shared_ptr<Clock> source, std::shared_ptr<Clock> dest) {
  FX_CHECK(source);
  FX_CHECK(dest);

  // For now we only adjust clocks in kExternalDomain (i.e. "client" clocks).
  std::shared_ptr<Clock> follower;
  if (source->adjustable() && source->domain() == Clock::kExternalDomain) {
    return Create(/*leader=*/dest, /*follower=*/source, Mode::WithAdjustments);
  }
  if (dest->adjustable() && dest->domain() == Clock::kExternalDomain) {
    return Create(/*leader=*/source, /*follower=*/dest, Mode::WithAdjustments);
  }

  // For MicroSRC, always express the adjustment relative to the source.
  return Create(/*leader=*/dest, /*follower=*/source, Mode::WithMicroSRC);
}

int32_t ClockSynchronizer::ClampPpm(int32_t ppm) {
  if (mode() == Mode::WithMicroSRC) {
    return std::clamp(ppm, -kMicroSrcAdjustmentPpmMax, kMicroSrcAdjustmentPpmMax);
  } else {
    return Clock::ClampZxClockPpm(ppm);
  }
}

int32_t ClockSynchronizer::ClampDoubleToPpm(double val) {
  return ClampPpm(static_cast<int32_t>(std::round(static_cast<double>(val) * 1e6)));
}

int32_t ClockSynchronizer::follower_adjustment_ppm() const {
  return last_adjustment_ppm_ ? *last_adjustment_ppm_ : 0;
}

bool ClockSynchronizer::NeedsSynchronization() const {
  FX_CHECK(state_on_reset_) << "must call Reset before Update";

  auto& follower_at_reset = state_on_reset_->follower_snapshot;
  auto& leader_at_reset = state_on_reset_->leader_snapshot;

  // Synchronization not needed if the leader and follower are the same clocks.
  if (follower_->koid() == leader_->koid()) {
    return false;
  }

  // Synchronization not needed if the leader and follower have identical rates and
  // haven't changed since the last Reset.
  if (follower_at_reset.to_clock_mono.rate() == leader_at_reset.to_clock_mono.rate() &&
      follower_at_reset.generation == follower_->to_clock_mono_snapshot().generation &&
      leader_at_reset.generation == leader_->to_clock_mono_snapshot().generation) {
    // Force synchronization if the Reset snapshot was not IdenticalToMonotonic.
    //
    // TODO(fxbug.dev/114920): This check is not necessary but preserves identical behavior
    // with the old code from audio_core. Some unit tests require this check because they
    // setup the clocks inconsistently relative to internal Mix parameters (e.g. some
    // MixStagePositionTests).
    if (!follower_->IdenticalToMonotonicClock() || !leader_->IdenticalToMonotonicClock()) {
      return true;
    }
    return false;
  }

  // Synchronization may be needed.
  return true;
}

void ClockSynchronizer::Reset(zx::time mono_now) {
  FX_CHECK(!last_mono_time_ || mono_now > *last_mono_time_)
      << "Reset at time " << mono_now.get() << " is not after the last Update or Reset at time "
      << last_mono_time_->get();

  pid_.Start(mono_now);
  last_mono_time_ = mono_now;
  state_on_reset_ = {
      .follower_snapshot = follower_->to_clock_mono_snapshot(),
      .leader_snapshot = leader_->to_clock_mono_snapshot(),
  };
}

void ClockSynchronizer::Update(zx::time mono_now, zx::duration follower_pos_error) {
  FX_CHECK(state_on_reset_) << "must call Reset before Update";
  FX_CHECK(mono_now > *last_mono_time_)
      << "Update at time " << mono_now.get() << " is not after the last Update or Reset at time "
      << last_mono_time_->get();

  auto adjust_ppm = ComputeNewAdjustPpm(mono_now, follower_pos_error);
  if (adjust_ppm) {
    LogClockAdjustment(*follower_, last_adjustment_ppm_, *adjust_ppm, follower_pos_error, pid_);
  }

  // Update the follower's clock rate if it changed.
  if (mode() == Mode::WithAdjustments && adjust_ppm) {
    if (last_adjustment_ppm_ != adjust_ppm) {
      follower_->SetRate(*adjust_ppm);
    }
  }

  last_adjustment_ppm_ = adjust_ppm;
  last_mono_time_ = mono_now;
}

std::optional<int32_t> ClockSynchronizer::ComputeNewAdjustPpm(zx::time mono_now,
                                                              zx::duration follower_pos_error) {
  constexpr bool kEnableFollowerPosErrChecks = false;

  if (!NeedsSynchronization()) {
    // TODO(fxbug.dev/114920): Enable this check. It currently cannot be enabled because unit tests
    // (e.g. mix_stage_unittest.cc) change internal Mix parameters (e.g. info.source_pos_error) in
    // ways that are inconsistent with the test's clocks.
    if constexpr (kEnableFollowerPosErrChecks) {
      FX_CHECK(follower_pos_error == zx::nsec(0))
          << "measured non-zero position error " << follower_pos_error.to_nsecs()
          << "ns when synchronization is not needed";
    }
    return std::nullopt;
  }

  // If the leader and follower are in the same clock domain, they have the same rate,
  // therefore they must not diverge.
  if (leader_->domain() == follower_->domain() && leader_->domain() != Clock::kExternalDomain) {
    // TODO(fxbug.dev/114920): Enable this check. See above comment.
    if constexpr (kEnableFollowerPosErrChecks) {
      FX_CHECK(follower_pos_error == zx::nsec(0))
          << "measured non-zero position error " << follower_pos_error.to_nsecs()
          << "ns from clocks in the same domain, where domain=" << leader_->domain();
    }
    return std::nullopt;
  }

  if (mode() == Mode::WithAdjustments && leader_->domain() == Clock::kMonotonicDomain) {
    // Converge position proportionally instead of the normal mechanism. Doing this rather than
    // allowing the PID to fully settle (and then locking to 0) gets us to tight sync faster.
    // See audio_clock_coefficients.h for an explanation of why positive error leads to a positive
    // clock rate adjustment.
    auto adjust_ppm =
        ClampPpm(static_cast<int32_t>(follower_pos_error / kLockToMonotonicErrorThreshold));
    // Not using the PID, so reset it.
    pid_.Start(mono_now);
    return adjust_ppm;
  }

  // Otherwise, use the PID to compute an adjustment.
  pid_.TuneForError(mono_now, static_cast<double>(follower_pos_error.to_nsecs()));
  auto adjust_ppm = ClampDoubleToPpm(pid_.Read());
  return adjust_ppm;
}

std::string ClockSynchronizer::ToDebugString() const {
  auto mono_to_follower_ref = follower_->to_clock_mono().Inverse();
  double mono_to_follower_rate = static_cast<double>(mono_to_follower_ref.subject_delta()) /
                                 static_cast<double>(mono_to_follower_ref.reference_delta());
  double follower_ppm = 1'000'000.0 * mono_to_follower_rate - 1'000'000.0;

  auto mono_to_leader_ref = leader_->to_clock_mono().Inverse();
  double mono_to_leader_rate = static_cast<double>(mono_to_leader_ref.subject_delta()) /
                               static_cast<double>(mono_to_leader_ref.reference_delta());
  double leader_ppm = 1'000'000.0 * mono_to_leader_rate - 1'000'000.0;

  std::stringstream os;
  os << "Mode " << mode() << ". Follower (0x" << follower_.get() << " " << follower_->name() << " "
     << follower_ppm << " ppm). Leader (0x" << leader_.get() << " " << leader_->name() << " "
     << leader_ppm << " ppm). ";
  if (last_adjustment_ppm_) {
    os << "Adjustment " << *last_adjustment_ppm_ << " ppm.";
  } else {
    os << "No adjustment yet.";
  }
  return os.str();
}

}  // namespace media_audio
