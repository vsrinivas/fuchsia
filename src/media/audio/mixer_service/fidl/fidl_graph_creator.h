// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_GRAPH_CREATOR_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_GRAPH_CREATOR_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <zircon/errors.h>

#include <optional>

#include "src/media/audio/mixer_service/fidl/ptr_decls.h"

namespace media_audio_mixer_service {

class FidlGraphCreator : public fidl::WireServer<fuchsia_audio_mixer::GraphCreator> {
 public:
  static FidlGraphCreatorPtr Create(async_dispatcher_t* fidl_thread_dispatcher,
                                    fidl::ServerEnd<fuchsia_audio_mixer::GraphCreator> server_end);

  // Shutdown this server.
  // This closes the channel, which eventually deletes this server.
  void Shutdown();

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::GraphCreator>.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override;

 private:
  FidlGraphCreator() = default;
  std::optional<fidl::ServerBindingRef<fuchsia_audio_mixer::GraphCreator>> binding_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_GRAPH_CREATOR_H_
