// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SYNCHRONIZER_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SYNCHRONIZER_H_

#include <lib/zx/time.h>

#include <memory>
#include <mutex>
#include <optional>
#include <ostream>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/pid_control.h"

namespace media_audio {

// Maintains synchronization between two clocks. Synchronization happens in two modes, "clock
// adjustment" and "MicroSRC", as detailed in //src/media/audio/mixer_service/docs/clocks.md.
//
// A call to `Reset(mono_reset_time)` declares that the leader and follower clock are assumed to be
// equivalent at the given time. From that point forward, the clocks may drift. It is the caller's
// responsibility to compute a position error, then regularly call `Update(mono_time, error)` to
// compute new rate adjustment parameters.
//
// This class is not safe for concurrent use.
class ClockSynchronizer {
 public:
  enum class Mode {
    // The follower is adjusted via `follower->SetRate` to match the leader.
    // The follower must be adjustable and must not be concurrently adjusted by a different leader.
    WithAdjustments,

    // Neither the follower nor leader is adjusted directly. Instead, rate adjustments are applied
    // during sample rate conversion ("SRC"), where the caller is using SRC to translate from a
    // source stream, which uses the `follower` clock, to a destination stream, which uses the
    // `leader` clock.
    WithMicroSRC,
  };

  // Creates a synchronizer with the given mode.
  static std::shared_ptr<ClockSynchronizer> Create(std::shared_ptr<Clock> leader,
                                                   std::shared_ptr<Clock> follower, Mode mode);

  // Given two clocks representing the source and destination side of a `Mixer` node, selects the
  // synchronization mode to use and calls `Create`.
  // TODO(fxbug.dev/114920): This is only for backwards compatibility with AudioCore's mixer
  // and can be removed after we have transitioned to the new mixer.
  static std::shared_ptr<ClockSynchronizer> SelectModeAndCreate(std::shared_ptr<Clock> source,
                                                                std::shared_ptr<Clock> dest);

  // Reports the mode that was set during Create.
  Mode mode() const { return mode_; }

  // Returns the follower clock.
  std::shared_ptr<Clock> follower() const { return follower_; }

  // Returns the leader clock.
  std::shared_ptr<Clock> leader() const { return leader_; }

  // Reports the follower's current adjustment in parts-per-million.
  // If mode is WithMicroSRC, this adjustment must be applied during SRC.
  int32_t follower_adjustment_ppm() const;

  // Resets all synchronization state at the given monotonic time. This method establishes
  // a relationship between the leader and follower clocks as described in the class comments.
  void Reset(zx::time mono_now);

  // Reports whether synchronization is needed.
  // Returns true only if it's possible that the clocks have diverged since the last Reset.
  // Must call Reset at least once before this method.
  bool NeedsSynchronization() const;

  // Checks if the follower clock is synchronized with the leader clock, and updates the
  // follower's clock rate if not. The caller is responsible for computing the follower's
  // position error. See class comments for more details.
  //
  // There must be at least one Reset before the first Update. The sequence of Reset and
  // Update calls must use monotonically-increasing values for `mono_now`.
  void Update(zx::time mono_now, zx::duration follower_pos_error);

  // Collects debugging info as a string.
  std::string ToDebugString() const;

 private:
  ClockSynchronizer(std::shared_ptr<Clock> leader, std::shared_ptr<Clock> follower, Mode mode,
                    media::audio::clock::PidControl::Coefficients pid_coefficients)
      : leader_(std::move(leader)),
        follower_(std::move(follower)),
        mode_(mode),
        pid_(pid_coefficients) {}

  int32_t ClampPpm(int32_t ppm);
  int32_t ClampDoubleToPpm(double val);
  std::optional<int32_t> ComputeNewAdjustPpm(zx::time mono_now, zx::duration follower_pos_error);

  const std::shared_ptr<Clock> leader_;
  const std::shared_ptr<Clock> follower_;
  const Mode mode_;

  media::audio::clock::PidControl pid_;
  std::optional<int32_t> last_adjustment_ppm_;
  std::optional<zx::time> last_mono_time_;

  struct StateOnReset {
    Clock::ToClockMonoSnapshot follower_snapshot;
    Clock::ToClockMonoSnapshot leader_snapshot;
  };
  std::optional<StateOnReset> state_on_reset_;
};

inline std::ostream& operator<<(std::ostream& out, ClockSynchronizer::Mode mode) {
  switch (mode) {
    case ClockSynchronizer::Mode::WithAdjustments:
      out << "WithAdjustments";
      break;
    case ClockSynchronizer::Mode::WithMicroSRC:
      out << "WithMicroSRC";
      break;
  }
  return out;
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SYNCHRONIZER_H_
