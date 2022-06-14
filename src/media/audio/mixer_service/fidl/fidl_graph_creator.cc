// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/fidl_graph_creator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media_audio {

// static
std::shared_ptr<FidlGraphCreator> FidlGraphCreator::Create(
    async_dispatcher_t* fidl_thread_dispatcher,
    fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end) {
  return BaseFidlServer::Create(fidl_thread_dispatcher, std::move(server_end));
}

void FidlGraphCreator::Create(CreateRequestView request, CreateCompleter::Sync& completer) {
  TRACE_DURATION("audio", "GraphCreator::Create");
  FX_CHECK(false) << "not implemented";
}

}  // namespace media_audio
