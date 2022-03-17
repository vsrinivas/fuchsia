// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/fidl_graph_creator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media_audio_mixer_service {

// static
std::shared_ptr<FidlGraphCreator> FidlGraphCreator::Create(
    async_dispatcher_t* fidl_thread_dispatcher,
    fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end) {
  // std::make_shared requires a public ctor, but we hide our ctor to force callers to use Create.
  struct WithPublicCtor : public FidlGraphCreator {};

  FidlGraphCreatorPtr server = std::make_shared<WithPublicCtor>();

  // Callback invoked when the server shuts down.
  auto on_unbound = [](FidlGraphCreator* server, fidl::UnbindInfo info,
                       fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end) {
    if (!info.is_user_initiated() && !info.is_peer_closed()) {
      FX_LOGS(ERROR) << "FidlGraphCreator shutdown with unexpected status: " << info;
    } else {
      FX_LOGS(DEBUG) << "FidlGraphCreator shutdown with status: " << info;
    }
  };

  // Passing server (a shared_ptr) to BindServer ensures that the server object
  // lives until on_unbound is called.
  server->binding_ = fidl::BindServer(fidl_thread_dispatcher, std::move(server_end), server,
                                      std::move(on_unbound));

  return server;
}

void FidlGraphCreator::Shutdown() {
  // Graceful shutdown: close the binding, which will (asynchronously) close the channel
  // and trigger OnUnbound, which will delete this server.
  binding_->Close(ZX_ERR_CANCELED);
}

void FidlGraphCreator::Create(CreateRequestView request, CreateCompleter::Sync& completer) {
  TRACE_DURATION("audio", "GraphCreator::Create");
  FX_CHECK(false) << "not implemented";
}

}  // namespace media_audio_mixer_service
