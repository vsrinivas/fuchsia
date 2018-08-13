// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_
#define GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_

#include <string>

#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

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

#endif  // GARNET_EXAMPLES_MEDIA_AUDIO_PLAYER_AUDIO_PLAYER_PARAMS_H_
