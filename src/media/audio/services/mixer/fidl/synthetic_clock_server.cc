// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/synthetic_clock_server.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/lib/clock/unadjustable_clock_wrapper.h"

namespace media_audio {

// static
std::shared_ptr<SyntheticClockServer> SyntheticClockServer::Create(
    std::shared_ptr<const FidlThread> thread,
    fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClock> server_end, std::shared_ptr<Clock> clock) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end), std::move(clock));
}

void SyntheticClockServer::Now(NowRequestView request, NowCompleter::Sync& completer) {
  fidl::Arena arena;
  completer.Reply(fuchsia_audio_mixer::wire::SyntheticClockNowResponse::Builder(arena)
                      .now(clock_->now().get())
                      .Build());
}

void SyntheticClockServer::SetRate(SetRateRequestView request, SetRateCompleter::Sync& completer) {
  if (!clock_->adjustable()) {
    completer.ReplyError(ZX_ERR_ACCESS_DENIED);
    return;
  }

  if (!request->has_rate_adjust_ppm()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  const auto rate_adjust_ppm = request->rate_adjust_ppm();
  if (rate_adjust_ppm < ZX_CLOCK_UPDATE_MIN_RATE_ADJUST ||
      rate_adjust_ppm > ZX_CLOCK_UPDATE_MAX_RATE_ADJUST) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  clock_->SetRate(rate_adjust_ppm);
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockSetRateResponse::Builder(arena).Build());
}

// static
std::shared_ptr<SyntheticClockRealmServer> SyntheticClockRealmServer::Create(
    std::shared_ptr<const FidlThread> thread,
    fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClockRealm> server_end) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end));
}

void SyntheticClockRealmServer::CreateClock(CreateClockRequestView request,
                                            CreateClockCompleter::Sync& completer) {
  std::string_view name;
  if (request->has_name()) {
    name = request->name().get();
  }

  if (!request->has_domain() || !request->has_adjustable()) {
    completer.ReplyError(fuchsia_audio_mixer::CreateClockError::kMissingField);
    return;
  }

  if (request->domain() == Clock::kMonotonicDomain && request->adjustable()) {
    completer.ReplyError(fuchsia_audio_mixer::CreateClockError::kMonotonicDomainIsNotAdjustable);
    return;
  }

  auto clock = realm_->CreateClock(name, request->domain(), request->adjustable());

  // This should not fail: since we just created `clock`, no other clock should have the same koid.
  auto add_result = registry_->AddClock(clock);
  FX_CHECK(add_result.is_ok()) << "Unexpected failure in ClockRegistry::AddClock: "
                               << add_result.status_string();

  // If the user wants explicit control, create a server.
  if (request->has_control()) {
    AddChildServer(
        SyntheticClockServer::Create(thread_ptr(), std::move(request->control()), clock));
  }

  // Since the underlying zx::clock does not represent the SyntheticClock's actual value, send the
  // client a zx::clock handle that is unreadable. The client should read the clock via their handle
  // to the SyntheticClockServer server.
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockResponse::Builder(arena)
          .handle(clock->DuplicateZxClockUnreadable())
          .Build());
}

void SyntheticClockRealmServer::ForgetClock(ForgetClockRequestView request,
                                            ForgetClockCompleter::Sync& completer) {
  if (!request->has_handle()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (auto result = registry_->ForgetClock(request->handle()); !result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockResponse::Builder(arena).Build());
}

void SyntheticClockRealmServer::ObserveClock(ObserveClockRequestView request,
                                             ObserveClockCompleter::Sync& completer) {
  if (!request->has_handle() || !request->has_observe()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto clock_result = registry_->FindClock(request->handle());
  if (!clock_result.is_ok()) {
    completer.ReplyError(clock_result.status_value());
    return;
  }

  // ObserveClock does not give permission to adjust.
  auto clock = std::make_shared<::media_audio::UnadjustableClockWrapper>(clock_result.value());
  AddChildServer(SyntheticClockServer::Create(thread_ptr(), std::move(request->observe()), clock));

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockResponse::Builder(arena).Build());
}

void SyntheticClockRealmServer::Now(NowRequestView request, NowCompleter::Sync& completer) {
  fidl::Arena arena;
  completer.Reply(fuchsia_audio_mixer::wire::SyntheticClockRealmNowResponse::Builder(arena)
                      .now(realm_->now().get())
                      .Build());
}

void SyntheticClockRealmServer::AdvanceBy(AdvanceByRequestView request,
                                          AdvanceByCompleter::Sync& completer) {
  if (!request->has_duration() || request->duration() <= 0) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  realm_->AdvanceBy(zx::nsec(request->duration()));
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByResponse::Builder(arena).Build());
}

}  // namespace media_audio
