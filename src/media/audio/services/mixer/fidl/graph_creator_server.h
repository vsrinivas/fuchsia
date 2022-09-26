// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_CREATOR_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_CREATOR_SERVER_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "src/media/audio/services/common/base_fidl_server.h"

namespace media_audio {

class GraphCreatorServer
    : public BaseFidlServer<GraphCreatorServer, fuchsia_audio_mixer::GraphCreator> {
 public:
  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<GraphCreatorServer> Create(
      std::shared_ptr<const FidlThread> thread,
      fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end);

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::GraphCreator>.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override;

 private:
  static inline constexpr std::string_view kName = "GraphCreatorServer";
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  GraphCreatorServer() = default;

  int64_t num_graphs_ = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GRAPH_CREATOR_SERVER_H_
