// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/gain_control.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>
#include <utility>
#include <variant>

#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {

void GainControl::Advance(zx::time reference_time) {
  FX_CHECK(reference_time >= last_advanced_time_)
      << "Advance reference_time=" << reference_time.get()
      << " < last_advanced_time=" << last_advanced_time_.get();

  // Apply all scheduled commands up to and including `reference_time`.
  const auto begin = scheduled_commands_.begin();
  const auto end = scheduled_commands_.upper_bound(reference_time);
  for (auto it = begin; it != end; ++it) {
    const auto& command_time = it->first;
    if (active_gain_ramp_ && active_gain_ramp_->end_time <= command_time) {
      // Command is past the end of the active gain ramp. Since it is guaranteed that the ramp has
      // started at a time `t >= last_applied_gain_command_time_`, we can complete the ramp here.
      CompleteActiveGainRamp();
    }
    ApplyCommand(command_time, it->second);
  }
  scheduled_commands_.erase(begin, end);
  AdvanceActiveGainRamp(reference_time);

  // Apply immediate commands.
  if (immediate_gain_command_) {
    ApplyGain(reference_time, immediate_gain_command_->gain_db, immediate_gain_command_->ramp);
    immediate_gain_command_ = std::nullopt;
  }
  if (immediate_mute_command_) {
    state_.is_muted = immediate_mute_command_->is_muted;
    immediate_mute_command_ = std::nullopt;
  }

  last_advanced_time_ = reference_time;
}

std::optional<zx::time> GainControl::NextScheduledStateChange() const {
  if (scheduled_commands_.empty()) {
    return std::nullopt;
  }
  return scheduled_commands_.begin()->first;
}

void GainControl::ScheduleGain(zx::time reference_time, float gain_db,
                               std::optional<GainRamp> ramp) {
  if (reference_time < last_advanced_time_) {
    FX_LOGS(WARNING) << "ScheduleGain at reference_time=" << reference_time.get()
                     << " < last_advanced_time=" << last_advanced_time_.get();
  }
  scheduled_commands_.emplace(reference_time, GainCommand{gain_db, ramp});
}

void GainControl::ScheduleMute(zx::time reference_time, bool is_muted) {
  if (reference_time < last_advanced_time_) {
    FX_LOGS(WARNING) << "ScheduleMute at reference_time=" << reference_time.get()
                     << " < last_advanced_time=" << last_advanced_time_.get();
  }
  scheduled_commands_.emplace(reference_time, MuteCommand{is_muted});
}

void GainControl::SetGain(float gain_db, std::optional<GainRamp> ramp) {
  immediate_gain_command_ = GainCommand{gain_db, ramp};
}

void GainControl::SetMute(bool is_muted) { immediate_mute_command_ = MuteCommand{is_muted}; }

void GainControl::AdvanceActiveGainRamp(zx::time reference_time) {
  if (!active_gain_ramp_) {
    return;
  }
  if (const int64_t nsecs_left = (active_gain_ramp_->end_time - reference_time).to_nsecs();
      nsecs_left > 0) {
    state_.gain_db =
        ScaleToDb(DbToScale(active_gain_ramp_->end_gain_db) -
                  static_cast<float>(nsecs_left) * active_gain_ramp_->linear_scale_slope_per_ns);
  } else {
    // Active gain ramp ends exactly at `reference_time`, we can complete the ramp here immediately.
    CompleteActiveGainRamp();
  }
}

void GainControl::ApplyCommand(zx::time reference_time, const Command& command) {
  if (std::holds_alternative<GainCommand>(command)) {
    if (reference_time >= last_applied_gain_command_time_) {
      // Make sure that we do *not* override any previously applied gain commands that were
      // scheduled at a time later than `reference_time`.
      last_applied_gain_command_time_ = reference_time;
      const auto& gain_command = std::get<GainCommand>(command);
      ApplyGain(reference_time, gain_command.gain_db, gain_command.ramp);
    }
  } else if (reference_time >= last_applied_mute_command_time_) {
    // Make sure that we do *not* override any previously applied mute commands that were scheduled
    // at a time later than `reference_time`.
    last_applied_mute_command_time_ = reference_time;
    state_.is_muted = std::get<MuteCommand>(command).is_muted;
  }
}

void GainControl::ApplyGain(zx::time reference_time, float gain_db,
                            const std::optional<GainRamp>& ramp) {
  if (!active_gain_ramp_ && gain_db == state_.gain_db) {
    // No state change will occur, we can skip processing further.
    return;
  }

  if (ramp && ramp->duration > zx::nsec(0)) {
    // Apply gain with ramp.
    FX_CHECK(ramp->type == GainRampType::kLinearScale)
        << "Unknown gain ramp type: " << static_cast<int>(ramp->type);
    AdvanceActiveGainRamp(reference_time);
    state_.linear_scale_slope_per_ns = (DbToScale(gain_db) - DbToScale(state_.gain_db)) /
                                       static_cast<float>(ramp->duration.to_nsecs());
    active_gain_ramp_ =
        ActiveGainRamp{reference_time + ramp->duration, gain_db, state_.linear_scale_slope_per_ns};
  } else {
    // No gain ramp needed, apply constant gain.
    state_.gain_db = gain_db;
    state_.linear_scale_slope_per_ns = 0.0f;
    active_gain_ramp_ = std::nullopt;
  }
}

void GainControl::CompleteActiveGainRamp() {
  state_.gain_db = active_gain_ramp_->end_gain_db;
  state_.linear_scale_slope_per_ns = 0.0f;
  active_gain_ramp_ = std::nullopt;
}

}  // namespace media_audio
