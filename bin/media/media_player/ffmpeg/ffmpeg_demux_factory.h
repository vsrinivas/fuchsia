// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DEMUX_FACTORY_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DEMUX_FACTORY_H_

#include "garnet/bin/media/media_player/demux/demux.h"

namespace media_player {

class FfmpegDemuxFactory : public DemuxFactory {
 public:
  // Creates an ffmpeg demux factory.
  static std::unique_ptr<DemuxFactory> Create(
      component::StartupContext* startup_context);

  FfmpegDemuxFactory();

  ~FfmpegDemuxFactory() override;

  // Creates a |Demux| object for a given reader.
  Result CreateDemux(std::shared_ptr<Reader> reader,
                     std::shared_ptr<Demux>* demux_out) override;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DEMUX_FACTORY_H_
