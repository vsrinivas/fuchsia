// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_
#define GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_

#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media_player.h>

#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"

namespace examples {

class AudioPlayer {
 public:
  AudioPlayer(const AudioPlayerParams& params, fxl::Closure quit_callback);

  ~AudioPlayer();

 private:
  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(uint64_t version = media::kInitialStatus,
                           media_player::MediaPlayerStatusPtr status = nullptr);

  fxl::Closure quit_callback_;
  media_player::MediaPlayerPtr media_player_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;
  bool quit_when_done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPlayer);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_
