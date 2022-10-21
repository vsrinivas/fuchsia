// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_SERVER_H_

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "lib/fidl/cpp/wire/wire_messaging_declarations.h"
#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

class GainControlServer
    : public BaseFidlServer<GainControlServer, fidl::WireServer, fuchsia_audio::GainControl> {
 public:
  // The returned server will live until the `server_end` channel is closed.
  struct Args {
    // Id of this gain control.
    GainControlId id;

    // Name of this gain control. Used for diagnostics only.
    std::string_view name;

    // Reference clock of this gain control.
    std::shared_ptr<Clock> reference_clock;

    // Global task queue to pass gain control commands into mixers.
    // TODO(fxbug.dev/87651): Consider using a dedicated `ThreadSafeQueue` in `MixerStage` instead.
    std::shared_ptr<GlobalTaskQueue> global_task_queue;
  };
  static std::shared_ptr<GainControlServer> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_audio::GainControl> server_end, Args args);

  // Adds the given `mixer` to this gain control.
  //
  // REQUIRED: `mixer->type() == Node::Type::kMixer`.
  void AddMixer(NodePtr mixer);

  // Removes the given `mixer` from this gain control.
  //
  // REQUIRED: `mixer->type() == Node::Type::kMixer`.
  void RemoveMixer(NodePtr mixer);

  // Implements `fidl::WireServer<fuchsia_audio::GainControl>`.
  void SetGain(SetGainRequestView request, SetGainCompleter::Sync& completer) final;
  void SetMute(SetMuteRequestView request, SetMuteCompleter::Sync& completer) final;

  // Returns the name of this gain control.
  std::string_view name() const { return name_; }

  // Returns the internal gain control.
  const GainControl& gain_control() const { return gain_control_; }

  // Returns the number of mixers that use this gain control.
  size_t num_mixers() const { return mixers_.size(); }

 private:
  static inline constexpr std::string_view kClassName = "GainControlServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  explicit GainControlServer(Args args);

  void ScheduleGain(zx::time reference_time, float gain_db, std::optional<GainRamp> ramp);
  void ScheduleMute(zx::time reference_time, bool is_muted);
  void SetGain(float gain_db, std::optional<GainRamp> ramp);
  void SetMute(bool is_muted);

  const GainControlId id_;
  const std::string name_;
  const std::shared_ptr<Clock> reference_clock_;
  GainControl gain_control_;
  std::shared_ptr<GlobalTaskQueue> global_task_queue_;

  // Mixers that use this gain control.
  std::unordered_set<NodePtr> mixers_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_SERVER_H_
