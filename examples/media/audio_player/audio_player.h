// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/fidl/net_media_player.fidl.h"
#include "lib/ftl/macros.h"

namespace examples {

class AudioPlayer {
 public:
  AudioPlayer(const AudioPlayerParams& params);

  ~AudioPlayer();

 private:
  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(
      uint64_t version = media::MediaPlayer::kInitialStatus,
      media::MediaPlayerStatusPtr status = nullptr);

  media::NetMediaPlayerPtr net_media_player_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;
  bool quit_when_done_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AudioPlayer);
};

}  // namespace examples
