// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl_realtime/gain_control_server.h"

#include <memory>
#include <optional>

#include "fidl/fuchsia.audio/cpp/common_types.h"
#include "fidl/fuchsia.audio/cpp/markers.h"
#include "fidl/fuchsia.audio/cpp/natural_types.h"
#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "lib/fpromise/result.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

using ::fuchsia_audio::GainError;

// static
std::shared_ptr<GainControlServer> GainControlServer::Create(
    std::shared_ptr<const FidlThread> thread,
    fidl::ServerEnd<fuchsia_audio::GainControl> server_end, UnreadableClock reference_clock) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end),
                                std::move(reference_clock));
}

void GainControlServer::Advance(zx::time reference_time) { gain_control_.Advance(reference_time); }

void GainControlServer::SetGain(SetGainRequestView request, SetGainCompleter::Sync& completer) {
  if (!request->has_how() || !request->has_when()) {
    FX_LOGS(WARNING) << "SetGain: missing field";
    completer.ReplyError(GainError::kMissingRequiredField);
    return;
  }

  const auto& how = request->how();
  float gain_db = kUnityGainDb;
  std::optional<GainRamp> ramp = std::nullopt;
  if (how.is_gain_db()) {
    gain_db = how.gain_db();
  } else if (how.is_ramped()) {
    const auto& ramped = how.ramped();
    if (!ramped.has_target_gain_db() || !ramped.has_duration() || !ramped.has_function()) {
      FX_LOGS(WARNING) << "SetGain: missing field in 'how.ramped'";
      completer.ReplyError(GainError::kMissingRequiredField);
      return;
    }

    if (!ramped.function().is_linear_slope()) {
      FX_LOGS(WARNING) << "SetGain: Unsupported option for 'how.ramped.function'";
      completer.ReplyError(GainError::kUnsupportedOption);
      return;
    }

    gain_db = ramped.target_gain_db();
    ramp = GainRamp{.duration = zx::duration(ramped.duration())};
  } else {
    FX_LOGS(WARNING) << "SetGain: Unsupported option for 'how'";
    completer.ReplyError(GainError::kUnsupportedOption);
    return;
  }

  const auto& when = request->when();
  if (when.is_immediately()) {
    gain_control_.SetGain(gain_db, ramp);
  } else if (when.is_timestamp()) {
    gain_control_.ScheduleGain(zx::time(when.timestamp()), gain_db, ramp);
  } else {
    FX_LOGS(WARNING) << "SetGain: Unsupported option for 'when'";
    completer.ReplyError(GainError::kUnsupportedOption);
    return;
  }

  fidl::Arena arena;
  completer.ReplySuccess(fuchsia_audio::wire::GainControlSetGainResponse::Builder(arena).Build());
}

void GainControlServer::SetMute(SetMuteRequestView request, SetMuteCompleter::Sync& completer) {
  if (!request->has_muted() || !request->has_when()) {
    FX_LOGS(WARNING) << "SetMute: missing field";
    completer.ReplyError(GainError::kMissingRequiredField);
    return;
  }

  const bool is_muted = request->muted();
  const auto& when = request->when();
  if (when.is_immediately()) {
    gain_control_.SetMute(is_muted);
  } else if (when.is_timestamp()) {
    gain_control_.ScheduleMute(zx::time(when.timestamp()), is_muted);
  } else {
    FX_LOGS(WARNING) << "SetMute: Unsupported option for 'when'";
    completer.ReplyError(GainError::kUnsupportedOption);
    return;
  }

  fidl::Arena arena;
  completer.ReplySuccess(fuchsia_audio::wire::GainControlSetMuteResponse::Builder(arena).Build());
}

GainControlServer::GainControlServer(UnreadableClock reference_clock)
    : gain_control_(std::move(reference_clock)) {}

}  // namespace media_audio
