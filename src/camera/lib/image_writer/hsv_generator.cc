// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_writer/hsv_generator.h"

#include <lib/image-format/image_format.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <array>
#include <iostream>

#include "src/camera/lib/image_writer/color_source.h"
#include "src/camera/lib/image_writer/image_format_rgba.h"

namespace camera {

constexpr uint8_t kTwoByteShift = 16;
constexpr uint8_t kOneByteShift = 8;
// Set the alpha value to 100%:
constexpr uint8_t kAlphaValue = 0xff;
constexpr uint8_t kOneByteMask = 0xff;
constexpr uint64_t kTwoByteMask = 0xffff;

// Write an image plane whose pixels are 2^n bytes wide.
template <class PixelSize>
void WriteByteAligned(void* start, PixelSize value, uint32_t width, uint32_t height,
                      uint32_t stride) {
  auto* pixels = reinterpret_cast<PixelSize*>(start);
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      pixels[row * stride / sizeof(value) + col] = value;
    }
  }
}

void WriteSingleColorImage(Rgba rgba, const fuchsia::sysmem::ImageFormat_2& format, void* buffer) {
  // Check if valid format
  uint32_t packed_value = RgbaPack(format.pixel_format.type, rgba);
  auto pixel_format_c = ConvertPixelFormatToC(format.pixel_format);
  uint32_t bytes_per_pixel = ImageFormatBitsPerPixel(&pixel_format_c) / 8;

  if (bytes_per_pixel == 4) {
    WriteByteAligned(buffer, packed_value, format.coded_width, format.coded_height,
                     format.bytes_per_row);
  }
  // 24 bit formats need to be handles specially, because we don't have a uint24_t:
  if (bytes_per_pixel == 3) {
    auto* pixels = reinterpret_cast<uint8_t*>(buffer);
    uint8_t color1 = packed_value >> kTwoByteShift & kOneByteMask;
    uint8_t color2 = packed_value >> kOneByteShift & kOneByteMask;
    uint8_t color3 = packed_value & kOneByteMask;
    for (uint32_t row = 0; row < format.coded_height; ++row) {
      for (uint32_t col = 0; col < format.coded_width; ++col) {
        uint32_t start = row * format.bytes_per_row + col * bytes_per_pixel;
        pixels[start] = color1;
        pixels[start + 1] = color2;
        pixels[start + 2] = color3;
      }
    }
  }
  if (bytes_per_pixel == 2) {
    uint16_t color = packed_value & kTwoByteMask;
    WriteByteAligned(buffer, color, format.coded_width, format.coded_height, format.bytes_per_row);
  }
  if (bytes_per_pixel == 1) {
    uint8_t color = packed_value & kOneByteMask;
    WriteByteAligned(buffer, color, format.coded_width, format.coded_height, format.bytes_per_row);
  }
}

zx_status_t HsvGenerator(void* start, const fuchsia::sysmem::ImageFormat_2& format,
                         uint32_t frame_index) {
  // Currently only support RGB colors
  if (!IsSupportedPixelFormat(format.pixel_format.type)) {
    FX_LOGS(ERROR) << "Unsupported format";
    return ZX_ERR_INVALID_ARGS;
  }
  // Get the color:
  Rgba color;
  ColorSource::hsv_color(frame_index, &color.r, &color.g, &color.b);
  color.a = kAlphaValue;
  WriteSingleColorImage(color, format, start);
  return ZX_OK;
}

}  // namespace camera
