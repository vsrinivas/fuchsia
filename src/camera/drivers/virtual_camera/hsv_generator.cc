// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/virtual_camera/hsv_generator.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <array>
#include <iostream>

#include <src/lib/syslog/cpp/logger.h>

#include "src/camera/drivers/virtual_camera/color_source.h"

namespace camera {

constexpr uint8_t kThreeByteShift = 24;
constexpr uint8_t kTwoByteShift = 16;
constexpr uint8_t kOneByteShift = 8;
// Set the alpha value to 100%:
constexpr uint8_t kAlphaValue = 0xff;
constexpr uint8_t kOneByteMask = 0xff;
constexpr uint64_t kTwoByteMask = 0xffff;
constexpr uint64_t kFourByteMask = 0xffffffff;

// Write an image plane whose pixels are 2^n bytes wide.
// This function assumes stride = width*bpp.
template <class PixelSize>
void WriteByteAligned(void* start, PixelSize value, uint32_t width, uint32_t height) {
  auto* pixels = reinterpret_cast<PixelSize*>(start);
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      pixels[row * width + col] = value;
    }
  }
}

zx_status_t RgbaPack(fuchsia::sysmem::PixelFormatType format, uint8_t r, uint8_t g, uint8_t b,
                     uint8_t a, uint32_t* out) {
  switch (format) {
    /// RGB only, 8 bits per each of R/G/B/A sample
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      *out = r << kThreeByteShift | g << kTwoByteShift | b << kOneByteShift | a;
      break;
    /// 32bpp BGRA, 1 plane.  RGB only, 8 bits per each of B/G/R/A sample.
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      *out = b << kThreeByteShift | g << kTwoByteShift | r << kOneByteShift | a;
      break;
    /// 24bpp BGR, 1 plane. RGB only, 8 bits per each of B/G/R sample
    case fuchsia::sysmem::PixelFormatType::BGR24:
      *out = b << kTwoByteShift | g << kOneByteShift | r;
      break;
    /// 16bpp RGB, 1 plane. 5 bits R, 6 bits G, 5 bits B
    case fuchsia::sysmem::PixelFormatType::RGB565:
      // Red: shift 8 bit value by 3 to get top 5 bits, then shift 6 + 5 over
      // Green: shift 8 bit value by 2 to get top 6 bits, then shift 5 over
      // Blue: shift 8 bit value by 3 to get top 5 bits
      *out = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
      break;
    /// 8bpp RGB, 1 plane. 3 bits R, 3 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB332:
      *out = (r >> 5) << 5 | (g >> 5) << 2 | (b >> 6);
      break;
    /// 8bpp RGB, 1 plane. 2 bits R, 2 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      *out = (r >> 6) << 6 | (g >> 6) << 4 | (b >> 6) << 2;
      break;
    default:
      FX_LOGS(ERROR) << "Unsupported format";
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

// Converts a packed RGB(A) format into 4 individual values.
// For formats that have fewer than 8 bits per color, the color values are shifted
// to their most significant bits.
zx_status_t RgbaUnpack(fuchsia::sysmem::PixelFormatType format, uint8_t* r, uint8_t* g, uint8_t* b,
                       uint8_t* a, uint32_t packed) {
  switch (format) {
    /// RGB only, 8 bits per each of R/G/B/A sample
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      // indices reversed because of endianness.
      *r = (reinterpret_cast<uint8_t*>(&packed))[3];
      *g = (reinterpret_cast<uint8_t*>(&packed))[2];
      *b = (reinterpret_cast<uint8_t*>(&packed))[1];
      *a = (reinterpret_cast<uint8_t*>(&packed))[0];
      break;
    /// 32bpp BGRA, 1 plane.  RGB only, 8 bits per each of B/G/R/A sample.
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      *b = (reinterpret_cast<uint8_t*>(&packed))[3];
      *g = (reinterpret_cast<uint8_t*>(&packed))[2];
      *r = (reinterpret_cast<uint8_t*>(&packed))[1];
      *a = (reinterpret_cast<uint8_t*>(&packed))[0];
      break;
    /// 24bpp BGR, 1 plane. RGB only, 8 bits per each of B/G/R sample
    case fuchsia::sysmem::PixelFormatType::BGR24:
      *a = 0;
      *b = (reinterpret_cast<uint8_t*>(&packed))[2];
      *g = (reinterpret_cast<uint8_t*>(&packed))[1];
      *r = (reinterpret_cast<uint8_t*>(&packed))[0];
      break;
    /// 16bpp RGB, 1 plane. 5 bits R, 6 bits G, 5 bits B
    case fuchsia::sysmem::PixelFormatType::RGB565:
      // Red: shift 8 bit value by 3 to get top 5 bits, then shift 6 + 5 over
      // Green: shift 8 bit value by 2 to get top 6 bits, then shift 5 over
      // Blue: shift 8 bit value by 3 to get top 5 bits
      *a = 0;
      *r = ((packed >> 11) << 3) & kOneByteMask;
      *g = ((packed >> 5) << 2) & kOneByteMask;
      *b = (packed << 3) & kOneByteMask;
      break;
    /// 8bpp RGB, 1 plane. 3 bits R, 3 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB332:
      *a = 0;
      *r = ((packed >> 5) << 5) & kOneByteMask;
      *g = ((packed >> 2) << 5) & kOneByteMask;
      *b = (packed << 6) & kOneByteMask;
      break;
    /// 8bpp RGB, 1 plane. 2 bits R, 2 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      *a = 0;
      *r = ((packed >> 6) << 6) & kOneByteMask;
      *g = ((packed >> 4) << 6) & kOneByteMask;
      *b = ((packed >> 2) << 6) & kOneByteMask;
      break;
    default:
      FX_LOGS(ERROR) << "Unsupported format";
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t HsvGenerator(void* start, const fuchsia::sysmem::ImageFormat_2& format,
                         uint64_t frame_index) {
  // Get the color:
  uint8_t r, g, b;
  ColorSource::hsv_color(frame_index, &r, &g, &b);
  uint32_t packed_value;
  zx_status_t status = RgbaPack(format.pixel_format.type, r, g, b, kAlphaValue, &packed_value);
  if (status != ZX_OK) {
    return status;
  }

  int bpp;
  switch (format.pixel_format.type) {
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      bpp = 4;
      break;
    case fuchsia::sysmem::PixelFormatType::BGR24:
      bpp = 3;
      break;
    case fuchsia::sysmem::PixelFormatType::RGB565:
      bpp = 2;
      break;
    case fuchsia::sysmem::PixelFormatType::RGB332:
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      bpp = 1;
      break;

    /// YUV formats unsupported for now:
    case fuchsia::sysmem::PixelFormatType::I420:
    case fuchsia::sysmem::PixelFormatType::M420:
    case fuchsia::sysmem::PixelFormatType::NV12:
    case fuchsia::sysmem::PixelFormatType::YUY2:
    case fuchsia::sysmem::PixelFormatType::YV12:
    default:
      FX_LOGS(ERROR) << "Unsupported format";
      return ZX_ERR_INVALID_ARGS;
  }

  if (bpp == 4) {
    auto* pixels = reinterpret_cast<uint32_t*>(start);
    uint32_t color = packed_value & kFourByteMask;
    WriteByteAligned(pixels, color, format.coded_width, format.coded_height);
  }
  // 24 bit formats need to be handles specially, because we don't have a uint24_t:
  if (bpp == 3) {
    auto* pixels = reinterpret_cast<uint8_t*>(start);
    uint8_t color1 = packed_value >> kTwoByteShift & kOneByteMask;
    uint8_t color2 = packed_value >> kOneByteShift & kOneByteMask;
    uint8_t color3 = packed_value & kOneByteMask;
    for (uint32_t row = 0; row < format.coded_height; ++row) {
      for (uint32_t col = 0; col < format.coded_width; ++col) {
        uint32_t start = bpp * (row * format.bytes_per_row / bpp + col);
        pixels[start] = color1;
        pixels[start + 1] = color2;
        pixels[start + 2] = color3;
      }
    }
  }
  if (bpp == 2) {
    auto* pixels = reinterpret_cast<uint16_t*>(start);
    uint16_t color = packed_value & kTwoByteMask;
    WriteByteAligned(pixels, color, format.coded_width, format.coded_height);
  }
  if (bpp == 1) {
    auto* pixels = reinterpret_cast<uint8_t*>(start);
    uint8_t color = packed_value & kOneByteMask;
    WriteByteAligned(pixels, color, format.coded_width, format.coded_height);
  }
  return ZX_OK;
}

}  // namespace camera
