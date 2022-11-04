// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_creator_server.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/graph_server.h"
#include "src/media/audio/services/mixer/fidl/real_clock_factory.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_factory.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_server.h"

namespace media_audio {

// static
std::shared_ptr<GraphCreatorServer> GraphCreatorServer::Create(
    std::shared_ptr<const FidlThread> thread,
    fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end) {
  return BaseFidlServer::Create(std::move(thread), std::move(server_end));
}

void GraphCreatorServer::Create(CreateRequestView request, CreateCompleter::Sync& completer) {
  TRACE_DURATION("audio", "GraphCreator::Create");
  ScopedThreadChecker checker(thread().checker());

  if (!request->has_graph()) {
    completer.ReplyError(fuchsia_audio_mixer::CreateGraphError::kInvalidGraphChannel);
    return;
  }

  GraphServer::Args args;

  ++num_graphs_;
  if (request->has_name()) {
    args.name = std::string(request->name().get());
  } else {
    args.name = "Graph" + std::to_string(num_graphs_);
  }

  auto fidl_thread = FidlThread::CreateFromNewThread(args.name + "-FidlThread");
  if (request->has_fidl_thread_deadline_profile() &&
      request->fidl_thread_deadline_profile().is_valid()) {
    fidl_thread->PostTask([profile = std::move(request->fidl_thread_deadline_profile())]() {
      if (auto status = zx::thread::self()->set_profile(profile, 0); status != ZX_OK) {
        FX_PLOGS(WARNING, status) << "Failed to set deadline profile for 'FidlThread'";
      }
    });
  }

  if (request->has_synthetic_clock_realm()) {
    auto realm =
        SyntheticClockRealmServer::Create(fidl_thread, std::move(request->synthetic_clock_realm()));
    args.clock_factory = std::make_shared<SyntheticClockFactory>(realm->realm());
    args.clock_registry = realm->registry();
  } else {
    args.clock_factory = std::make_shared<RealClockFactory>();
    args.clock_registry = std::make_shared<ClockRegistry>();
  }

  // Create a server to control this graph.
  // The created object will live until `args.sever_end` is closed.
  AddChildServer(
      GraphServer::Create(std::move(fidl_thread), std::move(request->graph()), std::move(args)));

  fidl::Arena arena;
  completer.ReplySuccess(
      fuchsia_audio_mixer::wire::GraphCreatorCreateResponse::Builder(arena).Build());
}

}  // namespace media_audio
