// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/types/stream_type.h"
#include "apps/media/src/framework/types/video_stream_type.h"
extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
}

// Ffmeg defines this...undefine.
#undef PixelFormat

namespace media {

struct AVCodecContextDeleter {
  void operator()(AVCodecContext* context) const {
    avcodec_free_context(&context);
  }
};

using AvCodecContextPtr =
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct AvCodecContext {
  static AvCodecContextPtr Create(const StreamType& stream_type);

  static std::unique_ptr<StreamType> GetStreamType(const AVCodecContext& from);

  static std::unique_ptr<StreamType> GetStreamType(
      const AVCodecParameters& from);
};

// Converts an AVPixelFormat to a PixelFormat.
VideoStreamType::PixelFormat PixelFormatFromAVPixelFormat(
    AVPixelFormat av_pixel_format);

// Converts a PixelFormat to an AVPixelFormat.
AVPixelFormat AVPixelFormatFromPixelFormat(
    VideoStreamType::PixelFormat pixel_format);

// Determines frame layout compatible with ffmpeg, returning the minimum
// payload size required to accommodate a decoded frame.
size_t LayoutFrame(VideoStreamType::PixelFormat pixel_format,
                   const VideoStreamType::Extent& coded_size,
                   std::vector<uint32_t>* line_stride_out,
                   std::vector<uint32_t>* plane_offset_out);

}  // namespace media
