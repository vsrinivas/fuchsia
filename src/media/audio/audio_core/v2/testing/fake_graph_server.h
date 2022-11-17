// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_FAKE_GRAPH_SERVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_FAKE_GRAPH_SERVER_H_

#include <fidl/fuchsia.audio.mixer/cpp/natural_messaging.h>
#include <fidl/fuchsia.audio.mixer/cpp/natural_types.h>

#include <variant>
#include <vector>

#include "src/media/audio/services/common/base_fidl_server.h"

namespace media_audio {

class FakeGraphServer
    : public BaseFidlServer<FakeGraphServer, fidl::Server, fuchsia_audio_mixer::Graph> {
 public:
  static std::shared_ptr<FakeGraphServer> Create(
      std::shared_ptr<const FidlThread> fidl_thread,
      fidl::ServerEnd<fuchsia_audio_mixer::Graph> server_end) {
    return BaseFidlServer::Create(std::move(fidl_thread), std::move(server_end));
  }

  // Log of all calls to this server.
  using CallType = std::variant<CreateProducerRequest,              //
                                CreateConsumerRequest,              //
                                CreateMixerRequest,                 //
                                CreateSplitterRequest,              //
                                CreateCustomRequest,                //
                                DeleteNodeRequest,                  //
                                CreateEdgeRequest,                  //
                                DeleteEdgeRequest,                  //
                                CreateThreadRequest,                //
                                DeleteThreadRequest,                //
                                CreateGainControlRequest,           //
                                DeleteGainControlRequest,           //
                                StartRequest,                       //
                                StopRequest,                        //
                                BindProducerLeadTimeWatcherRequest  //
                                >;

  const std::vector<CallType>& calls() const { return calls_; }

  // Implementation of fidl::Server<fuchsia_audio_mixer::Graph>.
  void CreateProducer(CreateProducerRequest& request,
                      CreateProducerCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateProducerResponse({.id = next_node_id_++})));
  }
  void CreateConsumer(CreateConsumerRequest& request,
                      CreateConsumerCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateConsumerResponse({.id = next_node_id_++})));
  }
  void CreateMixer(CreateMixerRequest& request, CreateMixerCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateMixerResponse({.id = next_node_id_++})));
  }
  void CreateSplitter(CreateSplitterRequest& request,
                      CreateSplitterCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateSplitterResponse({.id = next_node_id_++})));
  }
  void CreateCustom(CreateCustomRequest& request, CreateCustomCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateCustomResponse({.id = next_node_id_++})));
  }
  void DeleteNode(DeleteNodeRequest& request, DeleteNodeCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphDeleteNodeResponse()));
  }
  void CreateEdge(CreateEdgeRequest& request, CreateEdgeCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphCreateEdgeResponse()));
  }
  void DeleteEdge(DeleteEdgeRequest& request, DeleteEdgeCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphDeleteEdgeResponse()));
  }
  void CreateThread(CreateThreadRequest& request, CreateThreadCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateThreadResponse({.id = next_thread_id_++})));
  }
  void DeleteThread(DeleteThreadRequest& request, DeleteThreadCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphDeleteThreadResponse()));
  }
  void CreateGainControl(CreateGainControlRequest& request,
                         CreateGainControlCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(
        fuchsia_audio_mixer::GraphCreateGainControlResponse({.id = next_gain_control_id_++})));
  }
  void DeleteGainControl(DeleteGainControlRequest& request,
                         DeleteGainControlCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphDeleteGainControlResponse()));
  }
  void CreateGraphControlledReferenceClock(
      CreateGraphControlledReferenceClockCompleter::Sync& completer) final {
    completer.Reply(
        fit::ok(fuchsia_audio_mixer::GraphCreateGraphControlledReferenceClockResponse()));
  }
  void Start(StartRequest& request, StartCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphStartResponse()));
  }
  void Stop(StopRequest& request, StopCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok(fuchsia_audio_mixer::GraphStopResponse()));
  }
  void BindProducerLeadTimeWatcher(BindProducerLeadTimeWatcherRequest& request,
                                   BindProducerLeadTimeWatcherCompleter::Sync& completer) final {
    calls_.push_back(std::move(request));
    completer.Reply(fit::ok());
  }

 private:
  static inline constexpr std::string_view kClassName = "FakeGraphServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  FakeGraphServer() = default;

  std::vector<CallType> calls_;
  uint64_t next_node_id_ = 1;
  uint64_t next_thread_id_ = 1;
  uint64_t next_gain_control_id_ = 1;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_FAKE_GRAPH_SERVER_H_
