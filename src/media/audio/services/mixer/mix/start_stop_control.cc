// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/start_stop_control.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {
using When = StartStopControl::When;
}  // namespace

StartStopControl::StartStopControl(const Format& format, TimelineRate media_ticks_per_ns,
                                   UnreadableClock reference_clock)
    : format_(format),
      media_ticks_per_ns_(media_ticks_per_ns),
      frac_frames_per_media_ticks_(
          TimelineRate::Product(format_.frac_frames_per_ns(), media_ticks_per_ns.Inverse())),
      reference_clock_(reference_clock) {}

// static
void StartStopControl::CancelCommand(Command& command) {
  if (std::holds_alternative<StartCommand>(command)) {
    if (auto& cmd = std::get<StartCommand>(command); cmd.callback) {
      cmd.callback(fpromise::error(StartError::kCanceled));
    }
  } else {
    if (auto& cmd = std::get<StopCommand>(command); cmd.callback) {
      cmd.callback(fpromise::error(StopError::kCanceled));
    }
  }
}

void StartStopControl::Start(StartCommand cmd) {
  CancelPendingCommand();
  pending_ = std::move(cmd);
}

void StartStopControl::Stop(StopCommand cmd) {
  CancelPendingCommand();
  if (!is_started()) {
    if (cmd.callback) {
      cmd.callback(fpromise::error(StopError::kAlreadyStopped));
    }
    return;
  }
  pending_ = std::move(cmd);
}

std::optional<TimelineFunction> StartStopControl::presentation_time_to_frac_frame() const {
  if (last_start_command_) {
    return last_start_command_->presentation_time_to_frac_frame;
  }
  return std::nullopt;
}

void StartStopControl::AdvanceTo(const ClockSnapshots& clocks, zx::time reference_time) {
  FX_CHECK(!reference_time_now_ || reference_time >= *reference_time_now_)
      << "Time went backwards from " << *reference_time_now_ << " to " << reference_time;

  // Always update the current time.
  auto update_time = fit::defer([this, reference_time]() { reference_time_now_ = reference_time; });

  if (!pending_) {
    return;
  }

  auto [when, is_start] = PendingCommand(clocks.SnapshotFor(reference_clock_), reference_time);
  if (when.reference_time > reference_time) {
    return;
  }

  // The pending command occurs at or before `reference_time`, so it must be applied.
  if (std::holds_alternative<StartCommand>(*pending_)) {
    last_start_command_ = {
        .presentation_time_to_frac_frame = TimelineFunction(
            when.frame.raw_value(), when.reference_time.get(), format_.frac_frames_per_ns()),
        .start_reference_time = when.reference_time,
        .start_frame = when.frame,
    };
    if (auto& cmd = std::get<StartCommand>(*pending_); cmd.callback) {
      cmd.callback(fpromise::ok(when));
    }

  } else {
    last_start_command_ = std::nullopt;
    if (auto& cmd = std::get<StopCommand>(*pending_); cmd.callback) {
      cmd.callback(fpromise::ok(when));
    }
  }

  pending_ = std::nullopt;
}

std::optional<std::pair<When, StartStopControl::CommandType>> StartStopControl::PendingCommand(
    const ClockSnapshots& clocks) const {
  FX_CHECK(reference_time_now_);

  if (pending_) {
    return PendingCommand(clocks.SnapshotFor(reference_clock_), *reference_time_now_);
  }
  return std::nullopt;
}

void StartStopControl::CancelPendingCommand() {
  if (pending_) {
    CancelCommand(*pending_);
    pending_ = std::nullopt;
  }
}

std::pair<When, StartStopControl::CommandType> StartStopControl::PendingCommand(
    const ClockSnapshot& ref_clock, zx::time reference_time_for_immediate) const {
  FX_CHECK(pending_);

  if (std::holds_alternative<StartCommand>(*pending_)) {
    return std::make_pair(PendingStartCommand(ref_clock, std::get<StartCommand>(*pending_),
                                              reference_time_for_immediate),
                          CommandType::kStart);
  } else {
    return std::make_pair(PendingStopCommand(ref_clock, std::get<StopCommand>(*pending_),
                                             reference_time_for_immediate),
                          CommandType::kStop);
  }
}

When StartStopControl::PendingStartCommand(const ClockSnapshot& ref_clock, const StartCommand& cmd,
                                           zx::time reference_time_for_immediate) const {
  When when;
  SetStreamTime(when, StreamTimeToFrame(cmd.stream_time));

  // If the start time is not specified, it happens right now.
  if (!cmd.start_time) {
    when.reference_time = reference_time_for_immediate;
    when.mono_time = ref_clock.MonotonicTimeFromReferenceTime(when.reference_time);
    return when;
  }

  switch (cmd.start_time->clock) {
    case WhichClock::kSystemMonotonic:
      when.mono_time = cmd.start_time->time;
      when.reference_time = ref_clock.ReferenceTimeFromMonotonicTime(when.mono_time);
      break;
    case WhichClock::kReference:
      when.reference_time = cmd.start_time->time;
      when.mono_time = ref_clock.MonotonicTimeFromReferenceTime(when.reference_time);
      break;
    default:
      UNREACHABLE << "Bad WhichClock value " << static_cast<int>(cmd.start_time->clock);
  }

  return when;
}

When StartStopControl::PendingStopCommand(const ClockSnapshot& ref_clock, const StopCommand& cmd,
                                          zx::time reference_time_for_immediate) const {
  FX_CHECK(is_started());
  FX_CHECK(last_start_command_);

  const auto last_start_reference_time = last_start_command_->start_reference_time;
  const auto last_start_frame = last_start_command_->start_frame;
  When when;

  // If the stop time is at a specific frame, and that frame translates to a fractional nanosecond,
  // round up to the first reference time after the frame is presented.
  if (cmd.when && std::holds_alternative<StreamTime>(*cmd.when)) {
    SetStreamTime(when, StreamTimeToFrame(std::get<StreamTime>(*cmd.when)));
    when.reference_time = last_start_reference_time +
                          format_.duration_per(when.frame - last_start_frame,
                                               ::media::TimelineRate::RoundingMode::Ceiling);
    when.mono_time = ref_clock.MonotonicTimeFromReferenceTime(when.reference_time);
    return when;
  }

  // If the stop time is not specified, it happens right now.
  if (!cmd.when) {
    when.reference_time = reference_time_for_immediate;
    when.mono_time = ref_clock.MonotonicTimeFromReferenceTime(when.reference_time);
  } else {
    auto& real_time = std::get<RealTime>(*cmd.when);
    switch (real_time.clock) {
      case WhichClock::kSystemMonotonic:
        when.mono_time = real_time.time;
        when.reference_time = ref_clock.ReferenceTimeFromMonotonicTime(when.mono_time);
        break;
      case WhichClock::kReference:
        when.reference_time = real_time.time;
        when.mono_time = ref_clock.MonotonicTimeFromReferenceTime(when.reference_time);
        break;
      default:
        UNREACHABLE << "Bad WhichClock value " << static_cast<int>(real_time.clock);
    }
  }

  SetStreamTime(when, last_start_frame +
                          format_.frac_frames_per(when.reference_time - last_start_reference_time,
                                                  ::media::TimelineRate::RoundingMode::Floor));
  return when;
}

// Stream time rounding rules: when converting between a frame and stream time, round so the stream
// tim is not before the frame. Going from frame to stream time, round up. In the other direction,
// round down.

void StartStopControl::SetStreamTime(When& when, Fixed frame) const {
  when.stream_time = format_.duration_per(frame, TimelineRate::RoundingMode::Ceiling);
  when.packet_timestamp = frac_frames_per_media_ticks_.Inverse().Scale(
      frame.raw_value(), TimelineRate::RoundingMode::Ceiling);
  when.frame = frame;
}

Fixed StartStopControl::StreamTimeToFrame(StreamTime stream_time) const {
  if (std::holds_alternative<zx::duration>(stream_time)) {
    return format_.frac_frames_per(std::get<zx::duration>(stream_time),
                                   TimelineRate::RoundingMode::Floor);
  }
  if (std::holds_alternative<int64_t>(stream_time)) {
    return Fixed::FromRaw(frac_frames_per_media_ticks_.Scale(std::get<int64_t>(stream_time),
                                                             TimelineRate::RoundingMode::Floor));
  }
  return std::get<Fixed>(stream_time);
}

}  // namespace media_audio
