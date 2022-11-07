// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_GAIN_CONTROL_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_GAIN_CONTROL_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <map>
#include <optional>
#include <variant>
#include <vector>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {

enum class GainRampType {
  kLinearScale,  // Linear scale interpolation.
};

struct GainRamp {
  zx::duration duration;
  GainRampType type = GainRampType::kLinearScale;
};

// Class that controls audio gain. This essentially wraps the functionality of a FIDL GainControl.
//
// Gain can be controlled in two different ways:
//
//   1. by `ScheduleGain` and `ScheduleMute` functions:
//      These functions can be used to schedule gain and mute changes at a specified reference time
//      to be applied. When scheduling gain, an optional gain ramp parameter can be used, which
//      would apply a ramp with a specified duration, starting from the scheduled reference time,
//      from the gain value at the reference time, to the specified target gain. Note that the
//      starting gain value of the ramp is computed at the time of the next `Advance` call, in order
//      to make sure that all scheduled changes are taken into account at that reference time,
//      regardless of the order of the schedule calls.
//
//   2. by `SetGain` and `SetMute` functions:
//      These functions correspond to the "immediately" GainTimestamp option in FIDL GainControl
//      API. They can be used to directly apply a change in gain or mute. Note that, similar to
//      scheduling gains, an optional gain ramp parameter can be used when setting a change in gain,
//      which would start the specified ramp immediately in the next `Advance` call.
//
// The following are guaranteed when applying gain changes:
//
//   * `ScheduleGain` and `ScheduleMute` will always be applied in order of their reference times,
//     regardless of which order they were called. For example, these calls with decreasing
//     reference times below will be applied in the opposite order of their original call order:
//
//     ```
//     ScheduleGain(3, 3.0f);
//     ScheduleGain(2, 2.0f);
//     ScheduleMute(1, true);
//     ```
//
//   * Changes that are scheduled at the same reference time will be applied in their call order.
//     For example, calls below will result in a final gain value of 2.0f, which is muted:
//
//     ```
//     ScheduleGain(5, 3.0f);
//     ScheduleMute(5, false);
//     ScheduleGain(5, -10.0f);
//     ScheduleGain(5, 2.0f);
//     ScheduleMute(5, true);
//     ```
//
//   * Only a single gain ramp can be active at a time, i.e. any ongoing gain ramp at a time will be
//     replaced by a call that is set to be applied anytime at or after the beginning of the ongoing
//     ramp. This is not only true for the `ScheduleGain` and `ScheduleMute` calls, but also for the
//     `SetGain` and `SetMute` calls.
//
//   * Changes can be scheduled in the past, where the guarantees above will still be preserved.
//     That said, all the scheduled changes that were "late" to arrive will be applied before the
//     pending "immediately" set changes in the next `Advance` call.
//
//   * Likewise, `SetGain` and `SetMute` changes will typically be applied after `ScheduleGain` and
//     `ScheduleMute` changes that are set to be applied at the same reference time. However, since
//     `SetGain` and `SetMute` do not expose their reference time, we do *not* recommend mixing
//     these two types of functions if the call order is of importance to the application.
//
// This class is not safe for concurrent use.
class GainControl {
 public:
  struct State {
    float gain_db;
    bool is_muted;
    float linear_scale_slope_per_ns;

    bool operator!=(const State& other) const {
      return gain_db != other.gain_db || is_muted != other.is_muted ||
             linear_scale_slope_per_ns != other.linear_scale_slope_per_ns;
    }
  };

  explicit GainControl(UnreadableClock reference_clock)
      : reference_clock_(std::move(reference_clock)) {}

  // Advances state by applying all changes up to and including `reference_time`.
  void Advance(zx::time reference_time);

  // Returns next scheduled state change time, or nullopt if no changes are scheduled.
  std::optional<zx::time> NextScheduledStateChange() const;

  // Schedules gain at `reference_time` with an optional `ramp`.
  void ScheduleGain(zx::time reference_time, float gain_db,
                    std::optional<GainRamp> ramp = std::nullopt);

  // Schedules mute at `reference_time`.
  void ScheduleMute(zx::time reference_time, bool is_muted);

  // Sets gain *immediately* with an optional `ramp`.
  void SetGain(float gain_db, std::optional<GainRamp> ramp = std::nullopt);

  // Sets mute *immediately*.
  void SetMute(bool is_muted);

  // Returns the clock used by this gain control.
  [[nodiscard]] UnreadableClock reference_clock() const { return reference_clock_; }

  // Returns state.
  [[nodiscard]] const State& state() const { return state_; }

 private:
  struct GainCommand {
    float gain_db;
    std::optional<GainRamp> ramp;
  };
  struct MuteCommand {
    bool is_muted;
  };
  using Command = std::variant<GainCommand, MuteCommand>;

  struct ActiveGainRamp {
    zx::time end_time;
    float end_gain_db;
    float linear_scale_slope_per_ns;  // Corresponds to `GainRampType::kLinearScale` ramp type.
  };

  // Advances state to `reference_time` using the active gain ramp.
  void AdvanceActiveGainRamp(zx::time reference_time);

  // Applyies `command` at `reference_time`.
  void ApplyCommand(zx::time reference_time, const Command& command);

  // Applies gain at `reference_time` with an optional `ramp`.
  void ApplyGain(zx::time reference_time, float gain_db, const std::optional<GainRamp>& ramp);

  // Completes the active gain ramp.
  void CompleteActiveGainRamp();

  const UnreadableClock reference_clock_;

  // Commands to be applied *immediately* in the next `Advance` call. Since each consequent call to
  // `SetGain` or `SetMute` will override the previous call, we only need to store the last one.
  std::optional<GainCommand> immediate_gain_command_;
  std::optional<MuteCommand> immediate_mute_command_;

  // Sorted map of scheduled commands by their reference times.
  // TODO(fxbug.dev/113389): Make sure to prevent this from growing in an unbounded way.
  std::multimap<zx::time, Command> scheduled_commands_;

  std::optional<ActiveGainRamp> active_gain_ramp_;
  std::optional<zx::time> last_advanced_time_;
  std::optional<zx::time> last_applied_gain_command_time_;
  std::optional<zx::time> last_applied_mute_command_time_;
  State state_ = {/*gain_db=*/kUnityGainDb, /*is_muted=*/false, /*linear_scale_slope_per_ns=*/0.0f};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_GAIN_CONTROL_H_
