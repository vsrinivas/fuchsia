// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_PACKET_H_
#define SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_PACKET_H_

extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace mojo {
namespace media {
namespace ffmpeg {

struct AVPacketDeleter {
  inline void operator()(AVPacket* ptr) const { av_free_packet(ptr); }
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
}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_PACKET_H_
