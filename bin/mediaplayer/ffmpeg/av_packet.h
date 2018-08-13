// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_AV_PACKET_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_AV_PACKET_H_

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

namespace media_player {
namespace ffmpeg {

struct AVPacketDeleter {
  inline void operator()(AVPacket* ptr) const { av_packet_unref(ptr); }
};

using AvPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

struct AvPacket {
  static AvPacketPtr Create() {
    AVPacket* av_packet = new AVPacket();
    av_init_packet(av_packet);
    return AvPacketPtr(av_packet);
  }
};

}  // namespace ffmpeg
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_AV_PACKET_H_
