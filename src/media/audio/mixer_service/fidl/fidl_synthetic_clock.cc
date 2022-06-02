// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/fidl_synthetic_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/lib/clock/unadjustable_clock_wrapper.h"

namespace media_audio_mixer_service {

// static
std::shared_ptr<FidlSyntheticClock> FidlSyntheticClock::Create(
    async_dispatcher_t* fidl_thread_dispatcher,
    fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClock> server_end, std::shared_ptr<Clock> clock) {
  return BaseFidlServer::Create(fidl_thread_dispatcher, std::move(server_end), std::move(clock));
}

void FidlSyntheticClock::Now(NowRequestView request, NowCompleter::Sync& completer) {
  fidl::Arena arena;
  completer.Reply(fuchsia_audio_mixer::wire::SyntheticClockNowResponse::Builder(arena)
                      .now(clock_->now().get())
                      .Build());
}

void FidlSyntheticClock::SetRate(SetRateRequestView request, SetRateCompleter::Sync& completer) {
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
std::shared_ptr<FidlSyntheticClockRealm> FidlSyntheticClockRealm::Create(
    async_dispatcher_t* fidl_thread_dispatcher,
    fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClockRealm> server_end) {
  return BaseFidlServer::Create(fidl_thread_dispatcher, std::move(server_end));
}

void FidlSyntheticClockRealm::CreateClock(CreateClockRequestView request,
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

  std::unordered_set<std::shared_ptr<FidlSyntheticClock>> servers;
  if (request->has_control()) {
    servers.insert(FidlSyntheticClock::Create(dispatcher(), std::move(request->control()), clock));
  }

  clocks_[clock->koid()] = {
      std::make_shared<::media_audio::UnadjustableClockWrapper>(clock),
      std::move(servers),
  };

  // Since the underlying zx::clock does not represent the SyntheticClock's actual value, send the
  // client a zx::clock handle that is unreadable. The client should read the clock via their handle
  // to the FidlSyntheticClock server.
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockResponse::Builder(arena)
          .handle(clock->DuplicateZxClockUnreadable())
          .Build());
}

void FidlSyntheticClockRealm::ForgetClock(ForgetClockRequestView request,
                                          ForgetClockCompleter::Sync& completer) {
  if (!request->has_handle()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  const auto koid_result = ZxClockToKoid(request->handle());
  if (!koid_result.is_ok()) {
    completer.ReplyError(koid_result.status_value());
    return;
  }

  auto it = clocks_.find(koid_result.value());
  if (it == clocks_.end()) {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  for (auto& server : it->second.servers) {
    server->Shutdown();
  }

  clocks_.erase(it);
  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockResponse::Builder(arena).Build());
}

void FidlSyntheticClockRealm::ObserveClock(ObserveClockRequestView request,
                                           ObserveClockCompleter::Sync& completer) {
  if (!request->has_handle() || !request->has_observe()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  const auto koid_result = ZxClockToKoid(request->handle());
  if (!koid_result.is_ok()) {
    completer.ReplyError(koid_result.status_value());
    return;
  }

  auto it = clocks_.find(koid_result.value());
  if (it == clocks_.end()) {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  // ObserveClock does not give permission to adjust.
  auto clock = std::make_shared<::media_audio::UnadjustableClockWrapper>(it->second.clock);
  it->second.servers.insert(
      FidlSyntheticClock::Create(dispatcher(), std::move(request->observe()), clock));

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockResponse::Builder(arena).Build());
}

void FidlSyntheticClockRealm::Now(NowRequestView request, NowCompleter::Sync& completer) {
  fidl::Arena arena;
  completer.Reply(fuchsia_audio_mixer::wire::SyntheticClockRealmNowResponse::Builder(arena)
                      .now(realm_->now().get())
                      .Build());
}

void FidlSyntheticClockRealm::AdvanceBy(AdvanceByRequestView request,
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

zx::clock FidlSyntheticClockRealm::CreateGraphControlled() {
  auto clock =
      realm_->CreateClock(std::string("GraphControlled") + std::to_string(num_graph_controlled_),
                          Clock::kExternalDomain, /* adjustable = */ true);
  num_graph_controlled_++;
  clocks_[clock->koid()] = {.clock = clock};
  return clock->DuplicateZxClockUnreadable();
}

std::shared_ptr<Clock> FidlSyntheticClockRealm::FindOrCreate(zx::clock zx_clock,
                                                             std::string_view name,
                                                             uint32_t domain) {
  const auto koid_result = ZxClockToKoid(zx_clock);
  if (!koid_result.is_ok()) {
    return nullptr;
  }

  const auto koid = koid_result.value();
  if (auto it = clocks_.find(koid); it != clocks_.end()) {
    return it->second.clock;
  }

  // This is likely a client error: when the client is using a synthetic clock realm, all clocks
  // MUST be created by that realm, either via CreateClock or CreateGraphControlled.
  FX_LOGS(WARNING) << "clock not created by SyntheticClockRealm; koid=" << koid;
  return nullptr;
}

}  // namespace media_audio_mixer_service
