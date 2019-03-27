// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_FACTORY_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_FACTORY_H_

#include "src/media/playback/mediaplayer_tmp/demux/demux.h"
#include "src/media/playback/mediaplayer_tmp/demux/reader_cache.h"

namespace media_player {

class FfmpegDemuxFactory : public DemuxFactory {
 public:
  // Creates an ffmpeg demux factory.
  static std::unique_ptr<DemuxFactory> Create(
      component::StartupContext* startup_context);

  FfmpegDemuxFactory();

  ~FfmpegDemuxFactory() override;

  // Creates a |Demux| object for a given reader.
  Result CreateDemux(std::shared_ptr<ReaderCache> reader_cache,
                     std::shared_ptr<Demux>* demux_out) override;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DEMUX_FACTORY_H_
