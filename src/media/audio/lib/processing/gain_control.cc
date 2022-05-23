// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/gain_control.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>
#include <utility>
#include <variant>

#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {

void GainControl::Process(zx::time start_reference_time, zx::time end_reference_time,
                          const Callback& callback) {
  FX_CHECK(start_reference_time >= last_advanced_time_)
      << "Process start_reference_time=" << start_reference_time.get()
      << " < last_advanced_time=" << last_advanced_time_.get();
  FX_CHECK(start_reference_time < end_reference_time)
      << "Process start_reference_time=" << start_reference_time.get()
      << " >= end_reference_time=" << end_reference_time.get();

  // Update initial `state_` by processing scheduled commands up to and including
  // `start_reference_time`. This is to make sure that we do *not* miss any state changes that were
  // scheduled inorder in the past, even if they were scheduled at a time earlier than
  // `start_reference_time`, which can happen due to propagation delays in the processing pipeline.
  auto begin = scheduled_commands_.begin();
  auto end = scheduled_commands_.upper_bound(start_reference_time);
  for (auto it = begin; it != end; ++it) {
    const auto& command_time = it->first;
    if (active_gain_ramp_ && active_gain_ramp_->end_time <= command_time) {
      // Command is past the end of the active gain ramp. Since it is guaranteed that the ramp has
      // started at a time `t >= last_processed_gain_command_time_`, we can complete the ramp here.
      CompleteActiveGainRamp();
    }
    ProcessCommand(command_time, it->second);
  }
  AdvanceStateWithActiveGainRamp(start_reference_time);

  // Process immediate commands.
  if (immediate_gain_command_) {
    ProcessGain(start_reference_time, immediate_gain_command_->gain_db,
                immediate_gain_command_->ramp);
    immediate_gain_command_ = std::nullopt;
  }
  if (immediate_mute_command_) {
    state_.is_muted = immediate_mute_command_->is_muted;
    immediate_mute_command_ = std::nullopt;
  }

  // Report initial `state_` at `start_reference_time`.
  FX_CHECK(callback);
  callback(start_reference_time, state_);

  // Process the rest of the scheduled commands until `end_reference_time`.
  begin = end;
  end = scheduled_commands_.lower_bound(end_reference_time);
  if (begin != end) {
    // We keep track of `callback_time` and `callback_state` in order to minimize the number of
    // `callback` triggers. This is done by merging all state changes together for each
    // `callback_time`, and report it once via `callback` iff `state_` has been changed from the
    // previous `callback_state`.
    zx::time callback_time = begin->first;
    State callback_state = state_;
    for (auto it = begin; it != end; ++it) {
      // Trigger `callback` once whenever we move forward in time with an updated `state_`.
      const auto& command_time = it->first;
      if (command_time > callback_time) {
        if (state_ != callback_state) {
          callback(callback_time, state_);
          callback_state = state_;
        }
        callback_time = command_time;
      }

      if (active_gain_ramp_ && active_gain_ramp_->end_time <= command_time) {
        // Command is past the end of the active gain ramp.
        const auto end_time = active_gain_ramp_->end_time;
        CompleteActiveGainRamp();
        if (end_time < command_time && state_ != callback_state) {
          callback(end_time, state_);
          // No need to update `callback_time` again since this is a transition into `command_time`,
          // which is already handled above.
          callback_state = state_;
        }
      }
      ProcessCommand(command_time, it->second);
    }

    // Trigger `callback` if `state_` has been changed by the last `callback_time`.
    if (state_ != callback_state) {
      callback(callback_time, state_);
    }
  }

  // Clean up *all* scheduled commands until `end_reference_time`.
  if (scheduled_commands_.begin() != end) {
    scheduled_commands_.erase(scheduled_commands_.begin(), end);
  }

  if (active_gain_ramp_ && active_gain_ramp_->end_time < end_reference_time) {
    // Reference time advanced past the active gain ramp.
    const auto end_time = active_gain_ramp_->end_time;
    CompleteActiveGainRamp();
    callback(end_time, state_);
  }
  last_advanced_time_ = end_reference_time;
}

void GainControl::ScheduleGain(zx::time reference_time, float gain_db,
                               std::optional<GainRamp> ramp) {
  if (reference_time < last_advanced_time_) {
    FX_LOGS(WARNING) << "ScheduleGain at reference_time=" << reference_time.get()
                     << " < last_advanced_time=" << last_advanced_time_.get();
  }
  scheduled_commands_.emplace(reference_time, GainCommand{gain_db, std::move(ramp)});
}

void GainControl::ScheduleMute(zx::time reference_time, bool is_muted) {
  if (reference_time < last_advanced_time_) {
    FX_LOGS(WARNING) << "ScheduleMute at reference_time=" << reference_time.get()
                     << " < last_advanced_time=" << last_advanced_time_.get();
  }
  scheduled_commands_.emplace(reference_time, MuteCommand{is_muted});
}

void GainControl::SetGain(float gain_db, std::optional<GainRamp> ramp) {
  immediate_gain_command_ = GainCommand{gain_db, std::move(ramp)};
}

void GainControl::SetMute(bool is_muted) { immediate_mute_command_ = MuteCommand{is_muted}; }

void GainControl::AdvanceStateWithActiveGainRamp(zx::time reference_time) {
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

void GainControl::CompleteActiveGainRamp() {
  state_.gain_db = active_gain_ramp_->end_gain_db;
  state_.linear_scale_slope_per_ns = 0.0f;
  active_gain_ramp_ = std::nullopt;
}

void GainControl::ProcessCommand(zx::time reference_time, const Command& command) {
  if (std::holds_alternative<GainCommand>(command)) {
    if (reference_time >= last_processed_gain_command_time_) {
      // Make sure that we do *not* override any previously processed gain commands that were
      // scheduled at a time later than `reference_time`.
      last_processed_gain_command_time_ = reference_time;
      const auto& gain_command = std::get<GainCommand>(command);
      ProcessGain(reference_time, gain_command.gain_db, gain_command.ramp);
    }
  } else if (reference_time >= last_processed_mute_command_time_) {
    // Make sure that we do *not* override any previously processed mute commands that were
    // scheduled at a time later than `reference_time`.
    last_processed_mute_command_time_ = reference_time;
    state_.is_muted = std::get<MuteCommand>(command).is_muted;
  }
}

void GainControl::ProcessGain(zx::time reference_time, float gain_db,
                              const std::optional<GainRamp>& ramp) {
  if (!active_gain_ramp_ && gain_db == state_.gain_db) {
    // No state change will occur, we can skip processing further.
    return;
  }

  if (ramp && ramp->duration > zx::nsec(0)) {
    // Apply gain with ramp.
    FX_CHECK(ramp->type == GainRampType::kLinearScale)
        << "Unknown gain ramp type: " << static_cast<int>(ramp->type);
    AdvanceStateWithActiveGainRamp(reference_time);
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

}  // namespace media_audio
