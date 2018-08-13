// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_RENDER_VIDEO_CONVERTER_H_
#define GARNET_BIN_MEDIAPLAYER_RENDER_VIDEO_CONVERTER_H_

#include <memory>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"

namespace media_player {

class VideoConverter {
 public:
  VideoConverter();

  ~VideoConverter();

  // Sets the media type of the frames to be converted. 8-bit interleaved
  // RGBA output is assumed.
  void SetStreamType(std::unique_ptr<StreamType> stream_type);

  // Gets the size of the video.
  fuchsia::math::Size GetSize() const;

  // Gets the pixel aspect ratio of the video.
  fuchsia::math::Size GetPixelAspectRatio() const;

  // Converts the frame in the payload into the provided RGBA buffer.
  void ConvertFrame(uint8_t* rgba_buffer, uint32_t view_width,
                    uint32_t view_height, void* payload, uint64_t payload_size);

 private:
  // Builds the YUV-RGBA colorspace table.
  void BuildColorspaceTable();

  // Converts one line.
  void ConvertLine(uint32_t* dest_pixel, uint8_t* y_pixel, uint8_t* u_pixel,
                   uint8_t* v_pixel, uint32_t width);

  std::unique_ptr<StreamType> stream_type_;
  const VideoStreamType* video_stream_type_ = nullptr;
  std::unique_ptr<uint32_t[]> colorspace_table_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_RENDER_VIDEO_CONVERTER_H_
