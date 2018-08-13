// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_VIDEO_STREAM_TYPE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_VIDEO_STREAM_TYPE_H_

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "lib/fxl/logging.h"

namespace media_player {

// Describes the type of a video stream.
class VideoStreamType : public StreamType {
 public:
  enum class VideoProfile {
    kUnknown,
    kNotApplicable,
    kH264Baseline,
    kH264Main,
    kH264Extended,
    kH264High,
    kH264High10,
    kH264High422,
    kH264High444Predictive,
    kH264ScalableBaseline,
    kH264ScalableHigh,
    kH264StereoHigh,
    kH264MultiviewHigh
  };

  enum class PixelFormat {
    kUnknown,
    kI420,
    kYv12,
    kYv16,
    kYv12A,
    kYv24,
    kNv12,
    kNv21,
    kUyvy,
    kYuy2,
    kArgb,
    kXrgb,
    kRgb24,
    kRgb32,
    kMjpeg,
    kMt21
  };

  enum class ColorSpace {
    kUnknown,
    kNotApplicable,
    kJpeg,
    kHdRec709,
    kSdRec601
  };

  static const uint32_t kMaxPlaneIndex = 3;

  // Width and height.
  struct Extent {
    Extent() : width_(0), height_(0) {}
    Extent(int width, int height) : width_(width), height_(height) {}
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

   private:
    uint32_t width_;
    uint32_t height_;
  };

  // Specifies indices for each video plane.
  struct PlaneIndices {
    static const uint32_t kNone = kMaxPlaneIndex + 1;
    uint32_t argb_ = kNone;
    uint32_t y_ = kNone;
    uint32_t u_ = kNone;
    uint32_t v_ = kNone;
    uint32_t uv_ = kNone;
    uint32_t a_ = kNone;
  };

  // Information regarding a pixel format.
  struct PixelFormatInfo {
    // Returns the number of bytes per element for the specified plane.
    uint32_t bytes_per_element_for_plane(uint32_t plane) const {
      FXL_DCHECK(plane < plane_count_);
      return bytes_per_element_[plane];
    }

    // Returns the sample size of the specified plane.
    const Extent& sample_size_for_plane(uint32_t plane) const {
      FXL_DCHECK(plane < plane_count_);
      return sample_size_[plane];
    }

    // Returns the row count for the specified plane.
    uint32_t RowCount(uint32_t plane, uint32_t height) const;

    // Returns the column count for the specified plane.
    uint32_t ColumnCount(uint32_t plane, uint32_t width) const;

    // Returns the number of bytes per row for the specified plane.
    uint32_t BytesPerRow(uint32_t plane, uint32_t width) const;

    const uint32_t plane_count_;
    const PlaneIndices plane_indices_;
    const uint32_t bytes_per_element_[kMaxPlaneIndex + 1];
    const Extent sample_size_[kMaxPlaneIndex + 1];
  };

  // Gets information for the specified pixel format.
  static const PixelFormatInfo& InfoForPixelFormat(PixelFormat pixel_format);

  // Creates a VideoStreamType.
  static std::unique_ptr<StreamType> Create(
      const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters,
      VideoProfile profile, PixelFormat pixel_format, ColorSpace color_space,
      uint32_t width, uint32_t height, uint32_t coded_width,
      uint32_t coded_height, uint32_t pixel_aspect_ratio_width,
      uint32_t pixel_aspect_ratio_height,
      const std::vector<uint32_t> line_stride,
      const std::vector<uint32_t> plane_offset) {
    return std::unique_ptr<StreamType>(new VideoStreamType(
        encoding, std::move(encoding_parameters), profile, pixel_format,
        color_space, width, height, coded_width, coded_height,
        pixel_aspect_ratio_width, pixel_aspect_ratio_height, line_stride,
        plane_offset));
  }

  VideoStreamType(const std::string& encoding,
                  std::unique_ptr<Bytes> encoding_parameters,
                  VideoProfile profile, PixelFormat pixel_format,
                  ColorSpace color_space, uint32_t width, uint32_t height,
                  uint32_t coded_width, uint32_t coded_height,
                  uint32_t pixel_aspect_ratio_width,
                  uint32_t pixel_aspect_ratio_height,
                  const std::vector<uint32_t> line_stride,
                  const std::vector<uint32_t> plane_offset);

  ~VideoStreamType() override;

  const VideoStreamType* video() const override;

  VideoProfile profile() const { return profile_; }

  PixelFormat pixel_format() const { return pixel_format_; }

  ColorSpace color_space() const { return color_space_; }

  uint32_t width() const { return width_; }

  uint32_t height() const { return height_; }

  uint32_t coded_width() const { return coded_width_; }

  uint32_t coded_height() const { return coded_height_; }

  uint32_t pixel_aspect_ratio_width() const {
    return pixel_aspect_ratio_width_;
  }

  uint32_t pixel_aspect_ratio_height() const {
    return pixel_aspect_ratio_height_;
  }

  const std::vector<uint32_t>& line_stride() const { return line_stride_; }

  const std::vector<uint32_t>& plane_offset() const { return plane_offset_; }

  const PixelFormatInfo& pixel_format_info() { return pixel_format_info_; }

  std::unique_ptr<StreamType> Clone() const override;

  uint32_t line_stride_for_plane(uint32_t plane) const {
    FXL_DCHECK(plane < pixel_format_info_.plane_count_);
    return line_stride_[plane];
  }

  uint32_t plane_offset_for_plane(uint32_t plane) const {
    FXL_DCHECK(plane < pixel_format_info_.plane_count_);
    return plane_offset_[plane];
  }

  uint32_t line_stride_for_argb_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.argb_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.argb_);
  }

  uint32_t line_stride_for_y_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.y_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.y_);
  }

  uint32_t line_stride_for_u_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.u_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.u_);
  }

  uint32_t line_stride_for_v_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.v_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.v_);
  }

  uint32_t line_stride_for_uv_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.uv_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.uv_);
  }

  uint32_t line_stride_for_a_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.a_ <
               pixel_format_info_.plane_count_);
    return line_stride_for_plane(pixel_format_info_.plane_indices_.a_);
  }

  uint32_t plane_offset_for_argb_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.argb_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.argb_);
  }

  uint32_t plane_offset_for_y_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.y_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.y_);
  }

  uint32_t plane_offset_for_u_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.u_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.u_);
  }

  uint32_t plane_offset_for_v_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.v_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.v_);
  }

  uint32_t plane_offset_for_uv_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.uv_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.uv_);
  }

  uint32_t plane_offset_for_a_plane() const {
    FXL_DCHECK(pixel_format_info_.plane_indices_.a_ <
               pixel_format_info_.plane_count_);
    return plane_offset_for_plane(pixel_format_info_.plane_indices_.a_);
  }

 private:
  VideoProfile profile_;
  PixelFormat pixel_format_;
  ColorSpace color_space_;
  uint32_t width_;
  uint32_t height_;
  uint32_t coded_width_;
  uint32_t coded_height_;
  uint32_t pixel_aspect_ratio_width_;
  uint32_t pixel_aspect_ratio_height_;
  std::vector<uint32_t> line_stride_;
  std::vector<uint32_t> plane_offset_;
  const PixelFormatInfo& pixel_format_info_;
};

// Describes a set of video stream types.
class VideoStreamTypeSet : public StreamTypeSet {
 public:
  static std::unique_ptr<StreamTypeSet> Create(
      const std::vector<std::string>& encodings, Range<uint32_t> width,
      Range<uint32_t> height) {
    return std::unique_ptr<StreamTypeSet>(
        new VideoStreamTypeSet(encodings, width, height));
  }

  VideoStreamTypeSet(const std::vector<std::string>& encodings,
                     Range<uint32_t> width, Range<uint32_t> height);

  ~VideoStreamTypeSet() override;

  const VideoStreamTypeSet* video() const override;

  Range<uint32_t> width() const { return width_; }

  Range<uint32_t> height() const { return height_; }

  std::unique_ptr<StreamTypeSet> Clone() const override;

  bool Includes(const StreamType& type) const override;

 private:
  Range<uint32_t> width_;
  Range<uint32_t> height_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_VIDEO_STREAM_TYPE_H_
