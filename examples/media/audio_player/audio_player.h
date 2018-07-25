// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_
#define GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"

namespace examples {

class AudioPlayer {
 public:
  AudioPlayer(const AudioPlayerParams& params, fit::closure quit_callback);

  ~AudioPlayer();

 private:
  // Handles a status update from the player.
  void HandleStatusChanged(
      const fuchsia::mediaplayer::MediaPlayerStatus& status);

  // Logs a metadata property, if it exists.
  void MaybeLogMetadataProperty(const fuchsia::mediaplayer::Metadata& metadata,
                                const std::string& property_label,
                                const std::string& prefix);

  fit::closure quit_callback_;
  fuchsia::mediaplayer::MediaPlayerPtr media_player_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;
  bool quit_when_done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPlayer);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_H_
