// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_H_

#include <memory>

#include "src/media/playback/mediaplayer_tmp/demux/demux.h"
#include "src/media/playback/mediaplayer_tmp/demux/reader_cache.h"

namespace media_player {

class FfmpegDemux : public Demux {
 public:
  static std::shared_ptr<Demux> Create(
      std::shared_ptr<ReaderCache> reader_cache);

  ~FfmpegDemux() override {}
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_H_
