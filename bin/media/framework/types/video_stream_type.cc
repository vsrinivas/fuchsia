// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/media/src/framework/types/video_stream_type.h"

#include "apps/media/src/util/safe_clone.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

namespace {

static inline size_t RoundUpToAlign(size_t value, size_t alignment) {
  return ((value + (alignment - 1)) & ~(alignment - 1));
}

}  // namespace

// static
const VideoStreamType::PixelFormatInfo& VideoStreamType::InfoForPixelFormat(
    PixelFormat pixel_format) {
  struct Hash {
    std::size_t operator()(PixelFormat const& pixel_format) const {
      return static_cast<size_t>(pixel_format);
    }
  };
  static const std::unordered_map<PixelFormat, PixelFormatInfo, Hash> table = {
      {PixelFormat::kI420,
       {3, {1, 1, 1}, {Extent(1, 1), Extent(2, 2), Extent(2, 2)}}},
      {PixelFormat::kYv12,
       {3, {1, 1, 1}, {Extent(1, 1), Extent(2, 2), Extent(2, 2)}}},
      {PixelFormat::kYv16,
       {3, {1, 1, 1}, {Extent(1, 1), Extent(2, 1), Extent(2, 1)}}},
      {PixelFormat::kYv12A,
       {4,
        {1, 1, 1, 1},
        {Extent(1, 1), Extent(2, 2), Extent(2, 2), Extent(1, 1)}}},
      {PixelFormat::kYv24,
       {3, {1, 1, 1}, {Extent(1, 1), Extent(1, 1), Extent(1, 1)}}},
      {PixelFormat::kNv12, {2, {1, 2}, {Extent(1, 1), Extent(2, 2)}}},
      {PixelFormat::kNv21, {2, {1, 2}, {Extent(1, 1), Extent(2, 2)}}},
      {PixelFormat::kUyvy, {1, {2}, {Extent(1, 1)}}},
      {PixelFormat::kYuy2, {1, {2}, {Extent(1, 1)}}},
      {PixelFormat::kArgb, {1, {4}, {Extent(1, 1)}}},
      {PixelFormat::kXrgb, {1, {4}, {Extent(1, 1)}}},
      {PixelFormat::kRgb24, {1, {3}, {Extent(1, 1)}}},
      {PixelFormat::kRgb32, {1, {4}, {Extent(1, 1)}}},
      {PixelFormat::kMjpeg, {1, {0}, {Extent(1, 1)}}},
      {PixelFormat::kMt21, {2, {1, 2}, {Extent(1, 1), Extent(2, 2)}}}};

  FTL_DCHECK(table.find(pixel_format) != table.end());
  return table.find(pixel_format)->second;
}

size_t VideoStreamType::PixelFormatInfo::RowCount(size_t plane,
                                                  size_t height) const {
  FTL_DCHECK(plane < plane_count);
  const int sample_height = sample_size_for_plane(plane).height();
  return RoundUpToAlign(height, sample_height) / sample_height;
}

size_t VideoStreamType::PixelFormatInfo::ColumnCount(size_t plane,
                                                     size_t width) const {
  FTL_DCHECK(plane < plane_count);
  const size_t sample_width = sample_size_for_plane(plane).width();
  return RoundUpToAlign(width, sample_width) / sample_width;
}

size_t VideoStreamType::PixelFormatInfo::BytesPerRow(size_t plane,
                                                     size_t width) const {
  FTL_DCHECK(plane < plane_count);
  return bytes_per_element_for_plane(plane) * ColumnCount(plane, width);
}

VideoStreamType::Extent VideoStreamType::PixelFormatInfo::AlignedSize(
    const Extent& unaligned_size) const {
  const Extent alignment = CommonAlignment();
  const Extent adjusted =
      Extent(RoundUpToAlign(unaligned_size.width(), alignment.width()),
             RoundUpToAlign(unaligned_size.height(), alignment.height()));
  FTL_DCHECK((adjusted.width() % alignment.width() == 0) &&
             (adjusted.height() % alignment.height() == 0));
  return adjusted;
}

VideoStreamType::Extent VideoStreamType::PixelFormatInfo::CommonAlignment()
    const {
  size_t max_sample_width = 0;
  size_t max_sample_height = 0;
  for (size_t plane = 0; plane < plane_count; ++plane) {
    const Extent sample_size = sample_size_for_plane(plane);
    max_sample_width = std::max(max_sample_width, sample_size.width());
    max_sample_height = std::max(max_sample_height, sample_size.height());
  }
  return Extent(max_sample_width, max_sample_height);
}

void VideoStreamType::PixelFormatInfo::BuildFrameLayout(
    const Extent& coded_size,
    FrameLayout* frame_layout) const {
  FTL_DCHECK(frame_layout != nullptr);

  size_t size = 0;
  Extent aligned_size = AlignedSize(coded_size);

  for (size_t plane = 0; plane < plane_count; ++plane) {
    // The *2 in alignment for height is because some formats (e.g. h264)
    // allow interlaced coding, and then the size needs to be a multiple of two
    // macroblocks (vertically). See avcodec_align_dimensions2.
    const size_t height = RoundUpToAlign(RowCount(plane, aligned_size.height()),
                                         kFrameSizeAlignment * 2);
    frame_layout->line_stride[plane] = RoundUpToAlign(
        BytesPerRow(plane, aligned_size.width()), kFrameSizeAlignment);
    frame_layout->plane_offset[plane] = size;
    size += height * frame_layout->line_stride[plane];
  }

  // The extra line of UV being allocated is because h264 chroma MC overreads
  // by one line in some cases, see avcodec_align_dimensions2() and
  // h264_chromamc.asm:put_h264_chroma_mc4_ssse3().
  FTL_DCHECK(static_cast<size_t>(kUPlaneIndex) < plane_count);
  size += frame_layout->line_stride[kUPlaneIndex] + kFrameSizePadding;

  frame_layout->plane_count = plane_count;
  frame_layout->size = size;
}

VideoStreamType::VideoStreamType(const std::string& encoding,
                                 std::unique_ptr<Bytes> encoding_parameters,
                                 VideoProfile profile,
                                 PixelFormat pixel_format,
                                 ColorSpace color_space,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t coded_width,
                                 uint32_t coded_height)
    : StreamType(StreamType::Medium::kVideo,
                 encoding,
                 std::move(encoding_parameters)),
      profile_(profile),
      pixel_format_(pixel_format),
      color_space_(color_space),
      width_(width),
      height_(height),
      coded_width_(coded_width),
      coded_height_(coded_height) {}

VideoStreamType::~VideoStreamType() {}

const VideoStreamType* VideoStreamType::video() const {
  return this;
}

std::unique_ptr<StreamType> VideoStreamType::Clone() const {
  return Create(encoding(), SafeClone(encoding_parameters()), profile(),
                pixel_format(), color_space(), width(), height(), coded_width(),
                coded_height());
}

VideoStreamTypeSet::VideoStreamTypeSet(
    const std::vector<std::string>& encodings,
    Range<uint32_t> width,
    Range<uint32_t> height)
    : StreamTypeSet(StreamType::Medium::kVideo, encodings),
      width_(width),
      height_(height) {}

VideoStreamTypeSet::~VideoStreamTypeSet() {}

const VideoStreamTypeSet* VideoStreamTypeSet::video() const {
  return this;
}

std::unique_ptr<StreamTypeSet> VideoStreamTypeSet::Clone() const {
  return Create(encodings(), width(), height());
}

}  // namespace media
}  // namespace mojo
