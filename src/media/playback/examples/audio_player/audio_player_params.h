// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_
#define SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_

#include <string>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"

namespace examples {

class AudioPlayerParams {
 public:
  AudioPlayerParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  const std::string& url() const { return url_; }

  bool stay() const { return stay_; }

 private:
  void Usage();

  bool is_valid_;

  std::string url_;
  bool stay_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPlayerParams);
};

}  // namespace examples

#endif  // SRC_MEDIA_PLAYBACK_EXAMPLES_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_
