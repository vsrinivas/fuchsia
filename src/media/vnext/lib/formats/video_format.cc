// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/formats/video_format.h"

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

VideoFormat::VideoFormat(fuchsia::mediastreams::PixelFormat pixel_format,
                         fuchsia::mediastreams::ColorSpace color_space,
                         fuchsia::math::Size coded_size, fuchsia::math::Size display_size,
                         fuchsia::math::SizePtr aspect_ratio,
                         std::unique_ptr<Compression> compression,
                         std::unique_ptr<Encryption> encryption)
    : FormatBase(std::move(compression), std::move(encryption)),
      fidl_{.pixel_format = pixel_format,
            .color_space = color_space,
            .coded_size = coded_size,
            .display_size = display_size,
            .aspect_ratio = std::move(aspect_ratio)} {}

VideoFormat::VideoFormat(fuchsia::mediastreams::VideoFormat video_format,
                         fuchsia::mediastreams::CompressionPtr compression,
                         fuchsia::mediastreams::EncryptionPtr encryption)
    : FormatBase(std::move(compression), std::move(encryption)), fidl_(std::move(video_format)) {}

VideoFormat::VideoFormat(fuchsia::mediastreams::VideoFormat video_format, const FormatBase& base)
    : FormatBase(base), fidl_(std::move(video_format)) {}

VideoFormat::VideoFormat(const VideoFormat& other)
    : FormatBase(other), fidl_(fidl::Clone(other.fidl_)) {}

fuchsia::sysmem::PixelFormat VideoFormat::sysmem_pixel_format() const {
  switch (fidl_.pixel_format) {
    case fuchsia::mediastreams::PixelFormat::R8G8B8A8:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::BGRA32:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::BGRA32,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::I420:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::I420,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::M420:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::M420,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::NV12:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::NV12,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::YUY2:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::YUY2,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::MJPEG:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::MJPEG,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::YV12:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::YV12,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::BGR24:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::BGR24,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::RGB565:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::RGB565,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::RGB332:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::RGB332,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::RGB2220:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::RGB2220,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::L8:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::L8,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::R8:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::R8,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::R8G8:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::R8G8,
                                          .has_format_modifier = false};
    case fuchsia::mediastreams::PixelFormat::INVALID:
      return fuchsia::sysmem::PixelFormat{.type = fuchsia::sysmem::PixelFormatType::INVALID,
                                          .has_format_modifier = false};
  }
}

fuchsia::sysmem::ColorSpace VideoFormat::sysmem_color_space() const {
  switch (fidl_.color_space) {
    case fuchsia::mediastreams::ColorSpace::SRGB:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
    case fuchsia::mediastreams::ColorSpace::REC601_NTSC:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC};
    case fuchsia::mediastreams::ColorSpace::REC601_NTSC_FULL_RANGE:
      return fuchsia::sysmem::ColorSpace{
          .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE};
    case fuchsia::mediastreams::ColorSpace::REC601_PAL:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC601_PAL};
    case fuchsia::mediastreams::ColorSpace::REC601_PAL_FULL_RANGE:
      return fuchsia::sysmem::ColorSpace{
          .type = fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE};
    case fuchsia::mediastreams::ColorSpace::REC709:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC709};
    case fuchsia::mediastreams::ColorSpace::REC2020:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC2020};
    case fuchsia::mediastreams::ColorSpace::REC2100:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC2100};
    case fuchsia::mediastreams::ColorSpace::INVALID:
      return fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::INVALID};
  }
}

}  // namespace fmlib
