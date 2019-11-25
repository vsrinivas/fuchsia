// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_H_
#define SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include "lib/media/cpp/timeline_function.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/macros.h"
#include "src/media/playback/examples/audio_player/audio_player_params.h"

namespace examples {

class AudioPlayer {
 public:
  AudioPlayer(const AudioPlayerParams& params, fit::closure quit_callback);

  ~AudioPlayer();

 private:
  // Handles a status update from the player.
  void HandleStatusChanged(const fuchsia::media::playback::PlayerStatus& status);

  // Logs a metadata property, if it exists.
  void MaybeLogMetadataProperty(const fuchsia::media::Metadata& metadata,
                                const std::string& property_label, const std::string& prefix);

  void GetKeystroke();
  void HandleKeystroke(zx_status_t status, uint32_t events);

  fit::closure quit_callback_;
  fuchsia::media::playback::PlayerPtr player_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;
  bool quit_when_done_;

  fsl::FDWaiter keystroke_waiter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPlayer);
};

}  // namespace examples

#endif  // SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_H_
