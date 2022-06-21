// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_FIDL_GRAPH_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_FIDL_GRAPH_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "src/media/audio/services/common/base_fidl_server.h"

namespace media_audio {

class FidlGraph : public BaseFidlServer<FidlGraph, fuchsia_audio_mixer::Graph> {
 public:
  static std::shared_ptr<FidlGraph> Create(async_dispatcher_t* fidl_thread_dispatcher,
                                           fidl::ServerEnd<fuchsia_audio_mixer::Graph> server_end);

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
      CreateGraphControlledReferenceClockRequestView request,
      CreateGraphControlledReferenceClockCompleter::Sync& completer) override;
  void ForgetGraphControlledReferenceClock(
      ForgetGraphControlledReferenceClockRequestView request,
      ForgetGraphControlledReferenceClockCompleter::Sync& completer) override;

 private:
  static inline constexpr std::string_view Name = "FidlGraph";
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  FidlGraph() = default;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_FIDL_GRAPH_H_
