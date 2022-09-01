// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_SERVER_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/zx/profile.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "src/media/audio/lib/clock/timer.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/mixer/fidl/clock_registry.h"

namespace media_audio {

class GraphServer : public BaseFidlServer<GraphServer, fuchsia_audio_mixer::Graph> {
 public:
  struct Args {
    // Name of this graph.
    // For debugging only: may be empty or not unique.
    std::string name;

    // Deadline profile for the real-time FIDL thread.
    zx::profile realtime_fidl_thread_deadline_profile;

    // Registry for all clocks used by this graph.
    std::shared_ptr<ClockRegistry> clock_registry;
  };

  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<GraphServer> Create(std::shared_ptr<const FidlThread> main_fidl_thread,
                                             fidl::ServerEnd<fuchsia_audio_mixer::Graph> server_end,
                                             Args args);

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::Graph>.
  void CreateProducer(CreateProducerRequestView request,
                      CreateProducerCompleter::Sync& completer) override;
  void CreateConsumer(CreateConsumerRequestView request,
                      CreateConsumerCompleter::Sync& completer) override;
  void CreateMixer(CreateMixerRequestView request, CreateMixerCompleter::Sync& completer) override;
  void CreateSplitter(CreateSplitterRequestView request,
                      CreateSplitterCompleter::Sync& completer) override;
  void CreateCustom(CreateCustomRequestView request,
                    CreateCustomCompleter::Sync& completer) override;
  void DeleteNode(DeleteNodeRequestView request, DeleteNodeCompleter::Sync& completer) override;
  void CreateEdge(CreateEdgeRequestView request, CreateEdgeCompleter::Sync& completer) override;
  void DeleteEdge(DeleteEdgeRequestView request, DeleteEdgeCompleter::Sync& completer) override;
  void CreateThread(CreateThreadRequestView request,
                    CreateThreadCompleter::Sync& completer) override;
  void DeleteThread(DeleteThreadRequestView request,
                    DeleteThreadCompleter::Sync& completer) override;
  void CreateGainControl(CreateGainControlRequestView request,
                         CreateGainControlCompleter::Sync& completer) override;
  void DeleteGainControl(DeleteGainControlRequestView request,
                         DeleteGainControlCompleter::Sync& completer) override;
  void CreateGraphControlledReferenceClock(
      CreateGraphControlledReferenceClockCompleter::Sync& completer) override;
  void ForgetGraphControlledReferenceClock(
      ForgetGraphControlledReferenceClockRequestView request,
      ForgetGraphControlledReferenceClockCompleter::Sync& completer) override;

  // Name of this graph.
  // For debugging only: may be empty or not unique.
  std::string_view name() const { return name_; }

 private:
  static inline constexpr std::string_view kName = "GraphServer";
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  // Note: args.server_end is consumed by BaseFidlServer.
  GraphServer(Args args)
      : name_(std::move(args.name)), clock_registry_(std::move(args.clock_registry)) {}

  const std::string name_;

  std::shared_ptr<ClockRegistry> clock_registry_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_SERVER_H_
