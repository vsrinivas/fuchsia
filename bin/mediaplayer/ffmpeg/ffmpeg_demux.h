// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DEMUX_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DEMUX_H_

#include <memory>

#include "garnet/bin/mediaplayer/demux/demux.h"

namespace media_player {

class FfmpegDemux : public Demux {
 public:
  static std::shared_ptr<Demux> Create(std::shared_ptr<Reader> reader);

  ~FfmpegDemux() override {}
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DEMUX_H_
