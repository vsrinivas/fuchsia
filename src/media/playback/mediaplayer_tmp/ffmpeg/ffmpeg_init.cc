// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_init.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace media_player {

void InitFfmpeg() {
  static bool initialized = []() {
    av_register_all();
    return true;
  }();

  (void)initialized;
}

}  // namespace media_player
