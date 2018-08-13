// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_FRAME_LAYOUT_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_FRAME_LAYOUT_H_

#include <vector>

#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"
extern "C" {
#include "libavformat/avformat.h"
}

namespace media_player {

// Maintains frame buffer layout compatible with ffmpeg video decoders, updating
// as needed based on the codec context.
class FfmpegVideoFrameLayout {
 public:
  // Determines frame layout compatible with ffmpeg, returning the minimum
  // payload size required to accommodate a decoded frame.
  static size_t LayoutFrame(VideoStreamType::PixelFormat pixel_format,
                            const VideoStreamType::Extent& coded_size,
                            std::vector<uint32_t>* line_stride_out,
                            std::vector<uint32_t>* plane_offset_out);

  // Updates the layout as required to conform to the supplied context. Returns
  // true if the layout has changed.
  bool Update(const AVCodecContext& context);

  // Returns the buffer size required to accommodate a frame.
  size_t buffer_size() { return buffer_size_; }

  // Returns the line stride for each plane.
  const std::vector<uint32_t>& line_stride() { return line_stride_; }

  // Returns the buffer offset for each plane.
  const std::vector<uint32_t>& plane_offset() { return plane_offset_; }

 private:
  size_t buffer_size_ = 0;
  std::vector<uint32_t> line_stride_;
  std::vector<uint32_t> plane_offset_;

  // |Update| compares these values to the ones in the |AVCodecContext| to
  // determine if layout needs to be recalculated.
  AVPixelFormat pixel_format_;
  int coded_width_ = 0;
  int coded_height_ = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_FRAME_LAYOUT_H_
