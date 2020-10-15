// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_writer/image_format_rgba.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/algorithm.h>

namespace camera {

constexpr uint8_t kOneByteMask = 0xff;

uint8_t BitWidthToByteMask(uint8_t bit_width) {
  ZX_DEBUG_ASSERT(bit_width <= 8);
  return static_cast<uint8_t>(0xff << (8 - bit_width));
}

Rgba Min(const Rgba &a, const Rgba &b) {
  return {std::min(a.r, b.r), std::min(a.g, b.g), std::min(a.b, b.b), std::min(a.a, b.a)};
}

Rgba BitWidthToByteMask(Rgba bit_width) {
  return {BitWidthToByteMask(bit_width.r), BitWidthToByteMask(bit_width.g),
          BitWidthToByteMask(bit_width.b), BitWidthToByteMask(bit_width.a)};
}
// RgbaPixelFormatInfo stores all the information needed to encode an RGBA pixel.
struct RgbaPixelFormatInfo {
  // bit_width is the width in bits of each channel.
  // bit offset is the offset of the start of the channel from the start of the pixel.
  Rgba bit_width, bit_offset;
  uint8_t bytes_per_pixel;
  char name[9];  // used for debug messages

  uint32_t Pack(Rgba in) const {
    // For each color, first shift it over to get just the most significant (bit_width) pixels,
    // then shift it to the correct alignment
    return static_cast<uint32_t>(in.r >> (8 - bit_width.r)) << bit_offset.r |
           static_cast<uint32_t>(in.g >> (8 - bit_width.g)) << bit_offset.g |
           static_cast<uint32_t>(in.b >> (8 - bit_width.b)) << bit_offset.b |
           static_cast<uint32_t>(in.a >> (8 - bit_width.a)) << bit_offset.a;
  }

  Rgba Unpack(uint32_t packed) const {
    return {
        .r = static_cast<uint8_t>(((packed >> bit_offset.r) << (8 - bit_width.r)) & kOneByteMask),
        .g = static_cast<uint8_t>(((packed >> bit_offset.g) << (8 - bit_width.g)) & kOneByteMask),
        .b = static_cast<uint8_t>(((packed >> bit_offset.b) << (8 - bit_width.b)) & kOneByteMask),
        .a = static_cast<uint8_t>(((packed >> bit_offset.a) << (8 - bit_width.a)) & kOneByteMask)};
  }
};

const std::map<fuchsia::sysmem::PixelFormatType, RgbaPixelFormatInfo> kPixelFormatInfo = {
    {fuchsia::sysmem::PixelFormatType::R8G8B8A8, {{8, 8, 8, 8}, {24, 16, 8, 0}, 4u, "R8G8B8A8"}},
    {fuchsia::sysmem::PixelFormatType::BGRA32, {{8, 8, 8, 8}, {8, 16, 24, 0}, 4u, "BGRA32\0"}},
    {fuchsia::sysmem::PixelFormatType::BGR24, {{8, 8, 8, 0}, {16, 8, 0, 0}, 3u, "BGR24\0"}},
    {fuchsia::sysmem::PixelFormatType::RGB565, {{5, 6, 5, 0}, {11, 5, 0, 0}, 2u, "RGB565\0"}},
    {fuchsia::sysmem::PixelFormatType::RGB332, {{3, 3, 2, 0}, {5, 2, 0, 0}, 1u, "RGB332\0"}},
    {fuchsia::sysmem::PixelFormatType::RGB2220, {{2, 2, 2, 0}, {6, 4, 2, 0}, 1u, "RGB2220\0"}},
};

RgbaPixelFormatInfo GetPixelInfo(const fuchsia::sysmem::PixelFormatType &format) {
  ZX_ASSERT(kPixelFormatInfo.count(format));
  return kPixelFormatInfo.find(format)->second;
}

Rgba RgbaUnpack(fuchsia::sysmem::PixelFormatType format, uint32_t packed) {
  return GetPixelInfo(format).Unpack(packed);
}

uint32_t RgbaPack(fuchsia::sysmem::PixelFormatType format, Rgba in) {
  return GetPixelInfo(format).Pack(in);
}

bool ComparePacked(fuchsia::sysmem::PixelFormatType format1, uint32_t value1,
                   fuchsia::sysmem::PixelFormatType format2, uint32_t value2) {
  auto pixel_info1 = GetPixelInfo(format1);
  Rgba unpacked1 = pixel_info1.Unpack(value1);
  auto pixel_info2 = GetPixelInfo(format2);
  Rgba unpacked2 = pixel_info2.Unpack(value2);
  // Since the formats may have different bit widths, mask the unpacked values
  // so we only compare bits that both values share.
  Rgba min_mask = BitWidthToByteMask(Min(pixel_info1.bit_width, pixel_info2.bit_width));
  return (unpacked1 & min_mask) == (unpacked2 & min_mask);
}

std::string ToString(const fuchsia::sysmem::PixelFormatType &type) {
  return GetPixelInfo(type).name;
}

Rgba BitWidth(const fuchsia::sysmem::PixelFormatType &type) { return GetPixelInfo(type).bit_width; }

uint32_t BytesPerPixel(const fuchsia::sysmem::PixelFormatType &type) {
  return GetPixelInfo(type).bytes_per_pixel;
}
bool IsSupportedPixelFormat(const fuchsia::sysmem::PixelFormatType &type) {
  return kPixelFormatInfo.count(type);
}

std::vector<fuchsia::sysmem::PixelFormatType> GetSupportedFormats() {
  std::vector<fuchsia::sysmem::PixelFormatType> ret;
  for (auto &entry : kPixelFormatInfo) {
    ret.push_back(entry.first);
  }
  return ret;
}

fuchsia_sysmem_PixelFormat ConvertPixelFormatToC(const fuchsia::sysmem::PixelFormat &format) {
  return {.type = *reinterpret_cast<const fuchsia_sysmem_PixelFormatType *>(&format.type),
          .has_format_modifier = format.has_format_modifier,
          .format_modifier.value = format.format_modifier.value};
}

fuchsia_sysmem_ColorSpace ConvertColorSpaceToC(const fuchsia::sysmem::ColorSpace &cs) {
  return {*reinterpret_cast<const fuchsia_sysmem_ColorSpace *>(&cs.type)};
}

fuchsia_sysmem_ImageFormat_2 ConvertImageFormatToC(const fuchsia::sysmem::ImageFormat_2 &format) {
  return {
      .pixel_format = ConvertPixelFormatToC(format.pixel_format),
      .coded_width = format.coded_width,
      .coded_height = format.coded_height,
      .bytes_per_row = format.bytes_per_row,
      .display_width = format.display_width,
      .display_height = format.display_height,
      .layers = format.layers,
      .color_space = ConvertColorSpaceToC(format.color_space),
      .has_pixel_aspect_ratio = format.has_pixel_aspect_ratio,
      .pixel_aspect_ratio_width = format.pixel_aspect_ratio_width,
      .pixel_aspect_ratio_height = format.pixel_aspect_ratio_height,
  };
}

}  // namespace camera
