// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_video_frame_layout.h"

#include "garnet/bin/mediaplayer/ffmpeg/av_codec_context.h"
#include "lib/fxl/logging.h"

namespace media_player {

// static
size_t FfmpegVideoFrameLayout::LayoutFrame(
    const AVCodecContext& context,
    VideoStreamType::PixelFormat pixel_format,
    const VideoStreamType::Extent& coded_size,
    std::vector<uint32_t>* line_stride_out,
    std::vector<uint32_t>* plane_offset_out, uint32_t* coded_width_out,
    uint32_t* coded_height_out) {
  FXL_DCHECK(line_stride_out != nullptr);
  FXL_DCHECK(plane_offset_out != nullptr);
  FXL_DCHECK(coded_width_out != nullptr);
  FXL_DCHECK(coded_height_out != nullptr);

  int width = coded_size.width();
  int height = coded_size.height();

  avcodec_align_dimensions(const_cast<AVCodecContext*>(&context), &width,
                           &height);
  FXL_DCHECK(width >= 0 && static_cast<uint32_t>(width) >= coded_size.width());
  FXL_DCHECK(height >= 0 &&
             static_cast<uint32_t>(height) >= coded_size.height());

  const uint32_t new_coded_width = width;
  const uint32_t new_coded_height = height;

  const VideoStreamType::PixelFormatInfo& info =
      VideoStreamType::InfoForPixelFormat(pixel_format);

  line_stride_out->resize(info.plane_count_);
  plane_offset_out->resize(info.plane_count_);

  // TODO(dalesat): Get rid of superfluous stuff in VideoStreamType.

  // We need to use |LayoutPlaneIndexToYuv| here, because the we want the
  // strides and offsets in YUV order, but we calculate them in layout order.
  // There's a lengthy explanation of these terms above the declaration of
  // |VideoStreamType::PixelFormatInfo::LayoutPlaneIndexToYuv|.
  uint32_t plane_offset = 0;
  for (uint32_t layout_plane_index = 0; layout_plane_index < info.plane_count_;
       ++layout_plane_index) {
    uint32_t yuv_plane_index = info.LayoutPlaneIndexToYuv(layout_plane_index);
    uint32_t line_stride =
        info.BytesPerRow(layout_plane_index, new_coded_width);

    (*line_stride_out)[yuv_plane_index] = line_stride;
    (*plane_offset_out)[yuv_plane_index] = plane_offset;

    plane_offset +=
        info.RowCount(layout_plane_index, new_coded_height) * line_stride;
  }

  *coded_width_out = new_coded_width;
  *coded_height_out = new_coded_height;

  return static_cast<size_t>(plane_offset);
}

bool FfmpegVideoFrameLayout::Update(const AVCodecContext& context) {
  if (coded_width_ == context.coded_width &&
      coded_height_ == context.coded_height &&
      pixel_format_ == context.pix_fmt) {
    return false;
  }

  coded_width_ = context.coded_width;
  coded_height_ = context.coded_height;
  pixel_format_ = context.pix_fmt;

  uint32_t adjusted_coded_width_not_used;
  uint32_t adjusted_coded_height_not_used;
  buffer_size_ =
      LayoutFrame(context, PixelFormatFromAVPixelFormat(pixel_format_),
                  VideoStreamType::Extent(coded_width_, coded_height_),
                  &line_stride_, &plane_offset_, &adjusted_coded_width_not_used,
                  &adjusted_coded_height_not_used);

  return true;
}

}  // namespace media_player
