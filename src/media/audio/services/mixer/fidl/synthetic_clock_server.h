// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_SERVER_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <zircon/errors.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/clock_registry.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_factory.h"

namespace media_audio {

class SyntheticClockServer : public BaseFidlServer<SyntheticClockServer, fidl::WireServer,
                                                   fuchsia_audio_mixer::SyntheticClock> {
 public:
  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<SyntheticClockServer> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClock> server_end,
      std::shared_ptr<Clock> clock);

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::SyntheticClock>.
  void Now(NowRequestView request, NowCompleter::Sync& completer) override;
  void SetRate(SetRateRequestView request, SetRateCompleter::Sync& completer) override;

 private:
  static inline const std::string_view kName = "SyntheticClockRealmServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  explicit SyntheticClockServer(std::shared_ptr<Clock> clock) : clock_(std::move(clock)) {}

  // In practice, this should be either a SyntheticClock or an UnadjustableClockWrapper around a
  // SyntheticClock.
  const std::shared_ptr<Clock> clock_;
};

class SyntheticClockRealmServer : public BaseFidlServer<SyntheticClockRealmServer, fidl::WireServer,
                                                        fuchsia_audio_mixer::SyntheticClockRealm> {
 public:
  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<SyntheticClockRealmServer> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClockRealm> server_end);

  // Returns the underlying realm.
  std::shared_ptr<SyntheticClockRealm> realm() const { return realm_; }

  // Returns the clock registry used by this realm.
  std::shared_ptr<ClockRegistry> registry() const { return registry_; }

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::SyntheticClockRealm>.
  void CreateClock(CreateClockRequestView request, CreateClockCompleter::Sync& completer) override;
  void ForgetClock(ForgetClockRequestView request, ForgetClockCompleter::Sync& completer) override;
  void ObserveClock(ObserveClockRequestView request,
                    ObserveClockCompleter::Sync& completer) override;
  void Now(NowRequestView request, NowCompleter::Sync& completer) override;
  void AdvanceBy(AdvanceByRequestView request, AdvanceByCompleter::Sync& completer) override;

 private:
  static inline const std::string_view kName = "SyntheticClockRealmServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  SyntheticClockRealmServer() = default;

  std::shared_ptr<SyntheticClockRealm> realm_ = SyntheticClockRealm::Create();
  std::shared_ptr<ClockRegistry> registry_ = std::make_shared<ClockRegistry>();
  std::unordered_map<zx_koid_t, std::shared_ptr<Clock>> clocks_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_SERVER_H_
