// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
#define PERIDOT_BIN_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_

#include <vector>

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/media.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/lib/util/idle_waiter.h"


namespace modular {

using SpeechStatusCallback = std::function<void(SpeechStatus)>;

// Class in charge of playing media (speech) responses coming from query
// responses.
class MediaPlayer {
 public:
  // Instantiates a new MediaPlayer object.
  // |audio_server| is a binding to the audio server that will be used.
  // |debug| is used for debugging and testing purposes. Should be provided.
  MediaPlayer(media::AudioServerPtr audio_server,
              std::shared_ptr<SuggestionDebugImpl> debug);
  ~MediaPlayer();

  // Sets callback that is called whenever a change to SpeechStatus occurs.
  void SetSpeechStatusCallback(SpeechStatusCallback callback);

  // Plays media response coming from a query response.
  void PlayMediaResponse(MediaResponsePtr media_response);

 private:
  void HandleMediaUpdates(
      uint64_t version, media::MediaTimelineControlPointStatusPtr status);

  void OnMediaPacketProducerConnected(util::IdleWaiter::ActivityToken activity);

  media::AudioServerPtr audio_server_;
  media::MediaRendererPtr media_renderer_;
  media::MediaPacketProducerPtr media_packet_producer_;
  media::MediaTimelineControlPointPtr time_lord_;
  media::TimelineConsumerPtr media_timeline_consumer_;
  std::shared_ptr<SuggestionDebugImpl> debug_;
  SpeechStatusCallback speech_status_callback_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_MEDIA_PLAYER_MEDIA_PLAYER_IMPL_H_
