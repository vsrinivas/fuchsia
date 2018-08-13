// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"

#include "garnet/bin/mediaplayer/util/safe_clone.h"
#include "lib/fxl/logging.h"

namespace media_player {

namespace {

// Rounds up |value| to the nearest multiple of |alignment|. |alignment| must
// be a power of 2.
static inline uint32_t RoundUpToAlign(uint32_t value, uint32_t alignment) {
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
  // TODO(dalesat): Provide the plane_indices_ fields.
  static const std::unordered_map<PixelFormat, PixelFormatInfo, Hash> table = {
      {PixelFormat::kI420,
       {3, {}, {1, 1, 1}, {Extent(1, 1), Extent(2, 2), Extent(2, 2)}}},
      {PixelFormat::kYv12,
       {3,
        {.y_ = 0, .u_ = 2, .v_ = 1},
        {1, 1, 1},
        {Extent(1, 1), Extent(2, 2), Extent(2, 2)}}},
      {PixelFormat::kYv16,
       {3, {}, {1, 1, 1}, {Extent(1, 1), Extent(2, 1), Extent(2, 1)}}},
      {PixelFormat::kYv12A,
       {4,
        {},
        {1, 1, 1, 1},
        {Extent(1, 1), Extent(2, 2), Extent(2, 2), Extent(1, 1)}}},
      {PixelFormat::kYv24,
       {3, {}, {1, 1, 1}, {Extent(1, 1), Extent(1, 1), Extent(1, 1)}}},
      {PixelFormat::kNv12, {2, {}, {1, 2}, {Extent(1, 1), Extent(2, 2)}}},
      {PixelFormat::kNv21, {2, {}, {1, 2}, {Extent(1, 1), Extent(2, 2)}}},
      {PixelFormat::kUyvy, {1, {}, {2}, {Extent(1, 1)}}},
      {PixelFormat::kYuy2, {1, {}, {2}, {Extent(1, 1)}}},
      {PixelFormat::kArgb, {1, {}, {4}, {Extent(1, 1)}}},
      {PixelFormat::kXrgb, {1, {}, {4}, {Extent(1, 1)}}},
      {PixelFormat::kRgb24, {1, {}, {3}, {Extent(1, 1)}}},
      {PixelFormat::kRgb32, {1, {}, {4}, {Extent(1, 1)}}},
      {PixelFormat::kMjpeg, {1, {}, {0}, {Extent(1, 1)}}},
      {PixelFormat::kMt21, {2, {}, {1, 2}, {Extent(1, 1), Extent(2, 2)}}}};

  FXL_DCHECK(table.find(pixel_format) != table.end());
  return table.find(pixel_format)->second;
}

uint32_t VideoStreamType::PixelFormatInfo::RowCount(uint32_t plane,
                                                    uint32_t height) const {
  FXL_DCHECK(plane < plane_count_);
  const uint32_t sample_height = sample_size_for_plane(plane).height();
  return RoundUpToAlign(height, sample_height) / sample_height;
}

uint32_t VideoStreamType::PixelFormatInfo::ColumnCount(uint32_t plane,
                                                       uint32_t width) const {
  FXL_DCHECK(plane < plane_count_);
  const uint32_t sample_width = sample_size_for_plane(plane).width();
  return RoundUpToAlign(width, sample_width) / sample_width;
}

uint32_t VideoStreamType::PixelFormatInfo::BytesPerRow(uint32_t plane,
                                                       uint32_t width) const {
  FXL_DCHECK(plane < plane_count_);
  return bytes_per_element_for_plane(plane) * ColumnCount(plane, width);
}

VideoStreamType::VideoStreamType(
    const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters,
    VideoProfile profile, PixelFormat pixel_format, ColorSpace color_space,
    uint32_t width, uint32_t height, uint32_t coded_width,
    uint32_t coded_height, uint32_t pixel_aspect_ratio_width,
    uint32_t pixel_aspect_ratio_height, const std::vector<uint32_t> line_stride,
    const std::vector<uint32_t> plane_offset)
    : StreamType(StreamType::Medium::kVideo, encoding,
                 std::move(encoding_parameters)),
      profile_(profile),
      pixel_format_(pixel_format),
      color_space_(color_space),
      width_(width),
      height_(height),
      coded_width_(coded_width),
      coded_height_(coded_height),
      pixel_aspect_ratio_width_(pixel_aspect_ratio_width),
      pixel_aspect_ratio_height_(pixel_aspect_ratio_height),
      line_stride_(line_stride),
      plane_offset_(plane_offset),
      pixel_format_info_(InfoForPixelFormat(pixel_format)) {}

VideoStreamType::~VideoStreamType() {}

const VideoStreamType* VideoStreamType::video() const { return this; }

std::unique_ptr<StreamType> VideoStreamType::Clone() const {
  return Create(encoding(), SafeClone(encoding_parameters()), profile(),
                pixel_format(), color_space(), width(), height(), coded_width(),
                coded_height(), pixel_aspect_ratio_width(),
                pixel_aspect_ratio_height(), line_stride(), plane_offset());
}

VideoStreamTypeSet::VideoStreamTypeSet(
    const std::vector<std::string>& encodings, Range<uint32_t> width,
    Range<uint32_t> height)
    : StreamTypeSet(StreamType::Medium::kVideo, encodings),
      width_(width),
      height_(height) {}

VideoStreamTypeSet::~VideoStreamTypeSet() {}

const VideoStreamTypeSet* VideoStreamTypeSet::video() const { return this; }

std::unique_ptr<StreamTypeSet> VideoStreamTypeSet::Clone() const {
  return Create(encodings(), width(), height());
}

bool VideoStreamTypeSet::Includes(const StreamType& type) const {
  if (!StreamTypeSet::Includes(type)) {
    return false;
  }

  FXL_DCHECK(type.video() != nullptr);

  return width().contains(type.video()->width()) &&
         height().contains(type.video()->height());
}

}  // namespace media_player
