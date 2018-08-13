// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_video_frame_layout.h"

#include "garnet/bin/mediaplayer/ffmpeg/av_codec_context.h"
#include "lib/fxl/logging.h"

namespace media_player {

namespace {

constexpr uint32_t kFrameSizeAlignment = 16;
constexpr uint32_t kFrameSizePadding = 16;

// Rounds up |value| to the nearest multiple of |alignment|. |alignment| must
// be a power of 2.
inline uint32_t RoundUpToAlign(uint32_t value, uint32_t alignment) {
  return ((value + (alignment - 1)) & ~(alignment - 1));
}

VideoStreamType::Extent FfmpegCommonAlignment(
    const VideoStreamType::PixelFormatInfo& info) {
  uint32_t max_sample_width = 0;
  uint32_t max_sample_height = 0;
  for (uint32_t plane = 0; plane < info.plane_count_; ++plane) {
    const VideoStreamType::Extent sample_size =
        info.sample_size_for_plane(plane);
    max_sample_width = std::max(max_sample_width, sample_size.width());
    max_sample_height = std::max(max_sample_height, sample_size.height());
  }
  return VideoStreamType::Extent(max_sample_width, max_sample_height);
}

VideoStreamType::Extent FfmpegAlignedSize(
    const VideoStreamType::Extent& unaligned_size,
    const VideoStreamType::PixelFormatInfo& info) {
  const VideoStreamType::Extent alignment = FfmpegCommonAlignment(info);
  const VideoStreamType::Extent adjusted = VideoStreamType::Extent(
      RoundUpToAlign(unaligned_size.width(), alignment.width()),
      RoundUpToAlign(unaligned_size.height(), alignment.height()));
  FXL_DCHECK((adjusted.width() % alignment.width() == 0) &&
             (adjusted.height() % alignment.height() == 0));
  return adjusted;
}

}  // namespace

// static
size_t FfmpegVideoFrameLayout::LayoutFrame(
    VideoStreamType::PixelFormat pixel_format,
    const VideoStreamType::Extent& coded_size,
    std::vector<uint32_t>* line_stride_out,
    std::vector<uint32_t>* plane_offset_out) {
  FXL_DCHECK(line_stride_out != nullptr);
  FXL_DCHECK(plane_offset_out != nullptr);

  const VideoStreamType::PixelFormatInfo& info =
      VideoStreamType::InfoForPixelFormat(pixel_format);

  line_stride_out->resize(info.plane_count_);
  plane_offset_out->resize(info.plane_count_);

  uint32_t plane_offset = 0;
  VideoStreamType::Extent aligned_size = FfmpegAlignedSize(coded_size, info);

  for (uint32_t plane = 0; plane < info.plane_count_; ++plane) {
    // The *2 in alignment for height is because some formats (e.g. h264)
    // allow interlaced coding, and then the size needs to be a multiple of two
    // macroblocks (vertically). See avcodec_align_dimensions2.
    const uint32_t height = RoundUpToAlign(
        info.RowCount(plane, aligned_size.height()), kFrameSizeAlignment * 2);
    (*line_stride_out)[plane] = RoundUpToAlign(
        info.BytesPerRow(plane, aligned_size.width()), kFrameSizeAlignment);
    (*plane_offset_out)[plane] = plane_offset;
    plane_offset += height * (*line_stride_out)[plane];
  }

  // The extra line of UV being allocated is because h264 chroma MC overreads
  // by one line in some cases, see avcodec_align_dimensions2() and
  // h264_chromamc.asm:put_h264_chroma_mc4_ssse3().
  //
  // This is a bit of a hack and is really only here because of ffmpeg-specific
  // issues. It works because we happen to know that info.plane_count_ - 1 is
  // the U plane for the format we currently care about.
  return static_cast<size_t>(plane_offset +
                             (*line_stride_out)[info.plane_count_ - 1] +
                             kFrameSizePadding);
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

  buffer_size_ =
      LayoutFrame(PixelFormatFromAVPixelFormat(pixel_format_),
                  VideoStreamType::Extent(coded_width_, coded_height_),
                  &line_stride_, &plane_offset_);

  return true;
}

}  // namespace media_player
