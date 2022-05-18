// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_CODEC_CONTEXT_H_
#define SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_CODEC_CONTEXT_H_

#include <fuchsia/mediastreams/cpp/fidl.h>

#include "src/media/vnext/lib/formats/media_format.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/encryption_info.h"
}

// Ffmeg defines this...undefine.
#undef PixelFormat

namespace fmlib {

struct AVCodecContextDeleter {
  void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};

using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct AvCodecContext {
  static AvCodecContextPtr Create(const MediaFormat& format);

  static AvCodecContextPtr Create(const AudioFormat& format);

  static AvCodecContextPtr Create(const VideoFormat& format);

  static MediaFormat GetMediaFormat(const AVCodecContext& from);

  static MediaFormat GetMediaFormat(const AVStream& from);

  static std::vector<std::string> GetAudioDecoderCompressionTypes();

  static std::vector<std::string> GetVideoDecoderCompressionTypes();

  static std::vector<std::string> GetAudioEncoderCompressionTypes();

  static std::vector<std::string> GetVideoEncoderCompressionTypes();
};

// Converts an AVPixelFormat to a PixelFormat.
fuchsia::mediastreams::PixelFormat PixelFormatFromAVPixelFormat(AVPixelFormat av_pixel_format);

// Converts a PixelFormat to an AVPixelFormat.
AVPixelFormat AVPixelFormatFromPixelFormat(fuchsia::mediastreams::PixelFormat pixel_format);

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_CODEC_CONTEXT_H_
