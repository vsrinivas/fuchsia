// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/fidl_graph.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media_audio_mixer_service {

// static
std::shared_ptr<FidlGraph> FidlGraph::Create(
    async_dispatcher_t* fidl_thread_dispatcher,
    fidl::ServerEnd<fuchsia_audio_mixer::Graph> server_end) {
  return BaseFidlServer::Create(fidl_thread_dispatcher, std::move(server_end));
}

void FidlGraph::CreateProducer(CreateProducerRequestView request,
                               CreateProducerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateProducer");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateConsumer(CreateConsumerRequestView request,
                               CreateConsumerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateConsumer");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateMixer(CreateMixerRequestView request, CreateMixerCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateMixer");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateSplitter(CreateSplitterRequestView request,
                               CreateSplitterCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateSplitter");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateCustom(CreateCustomRequestView request,
                             CreateCustomCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateCustom");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::DeleteNode(DeleteNodeRequestView request, DeleteNodeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteNode");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateEdge(CreateEdgeRequestView request, CreateEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateEdge");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::DeleteEdge(DeleteEdgeRequestView request, DeleteEdgeCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteEdge");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateThread(CreateThreadRequestView request,
                             CreateThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateThread");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::DeleteThread(DeleteThreadRequestView request,
                             DeleteThreadCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteThread");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateGainControl(CreateGainControlRequestView request,
                                  CreateGainControlCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateGainControl");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::DeleteGainControl(DeleteGainControlRequestView request,
                                  DeleteGainControlCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::DeleteGainControl");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::CreateGraphControlledReferenceClock(
    CreateGraphControlledReferenceClockRequestView request,
    CreateGraphControlledReferenceClockCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::CreateGraphControlledReferenceClock");
  FX_CHECK(false) << "not implemented";
}

void FidlGraph::ForgetGraphControlledReferenceClock(
    ForgetGraphControlledReferenceClockRequestView request,
    ForgetGraphControlledReferenceClockCompleter::Sync& completer) {
  TRACE_DURATION("audio", "Graph:::ForgetGraphControlledReferenceClock");
  FX_CHECK(false) << "not implemented";
}

}  // namespace media_audio_mixer_service
