// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_

#include <vector>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>

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

  // Plays audio response coming from a query response.
  void PlayAudioResponse(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer2> audio_response);

 private:
  fuchsia::media::AudioPtr audio_;

  // Suggestion Engine maintains ownership of an |AudioRendererPtr| during
  // playback to enforce policy and have visibility into playback status (via
  // whether or not the channel is closed). Only one agent is allowed to play
  // responses at a time.
  fuchsia::media::AudioRenderer2Ptr audio_renderer_;
  std::unique_ptr<fidl::Binding<fuchsia::media::AudioRenderer2>>
      audio_renderer_binding_;

  std::shared_ptr<SuggestionDebugImpl> debug_;
  SpeechStatusCallback speech_status_callback_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_MEDIA_PLAYER_H_
