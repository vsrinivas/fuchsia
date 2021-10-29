// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_init.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace media_player {

// TODO(fxr/87639): remove entire function once we're committed to the new version.
void InitFfmpeg() {
  static bool initialized = []() {
#if LIBAVFORMAT_VERSION_MAJOR == 58
    // TODO(dalesat): Get rid of |InitFfmpeg| when we don't have to support V58 anymore.
    av_register_all();
#endif
    return true;
  }();

  (void)initialized;
}

}  // namespace media_player
