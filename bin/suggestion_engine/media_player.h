// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_

#include <vector>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/lib/util/idle_waiter.h"

namespace modular {

using SpeechStatusCallback =
    std::function<void(fuchsia::modular::SpeechStatus)>;

// Class in charge of playing media (speech) responses coming from query
// responses.
class MediaPlayer {
 public:
  // Instantiates a new MediaPlayer object.
  // |audio| is a binding to the audio service that will be used.
  // |debug| is used for debugging and testing purposes. Should be provided.
  MediaPlayer(fuchsia::media::AudioPtr audio,
              std::shared_ptr<SuggestionDebugImpl> debug);
  ~MediaPlayer();

  // Sets callback that is called whenever a change to
  // fuchsia::modular::SpeechStatus occurs.
  void SetSpeechStatusCallback(SpeechStatusCallback callback);

  // Plays media response coming from a query response.
  void PlayMediaResponse(fuchsia::modular::MediaResponsePtr media_response);

 private:
  void HandleMediaUpdates(
      uint64_t version,
      fuchsia::media::MediaTimelineControlPointStatusPtr status);

  void OnMediaPacketProducerConnected(util::IdleWaiter::ActivityToken activity);

  fuchsia::media::AudioPtr audio_;
  fuchsia::media::MediaRendererPtr media_renderer_;
  fuchsia::media::MediaPacketProducerPtr media_packet_producer_;
  fuchsia::media::MediaTimelineControlPointPtr time_lord_;
  fuchsia::media::TimelineConsumerPtr media_timeline_consumer_;
  std::shared_ptr<SuggestionDebugImpl> debug_;
  SpeechStatusCallback speech_status_callback_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_
