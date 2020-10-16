// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/camera/lib/image_utils/hsv_generator.h"
#include "src/camera/lib/image_utils/image_format_rgba.h"

namespace camera {
namespace {

bool ComparePacked(fuchsia::sysmem::PixelFormatType format1, uint32_t value1,
                   fuchsia::sysmem::PixelFormatType format2, uint32_t value2) {
  Rgba unpacked1 = RgbaUnpack(format1, value1);
  Rgba unpacked2 = RgbaUnpack(format2, value2);
  // Since the formats may have different bit_widths, mask the unpacked values
  // so we only compare bits that both values share.
  Rgba min_mask = BitWidthToByteMask(Min(BitWidth(format1), BitWidth(format2)));
  return (unpacked1 & min_mask) == (unpacked2 & min_mask);
}

// Fixed test colors using common patterns.
constexpr Rgba kTestColors[]{
    {0x00, 0x00, 0x00, 0x00},  // transparent black
    {0xFF, 0xFF, 0xFF, 0x00},  // transparent white
    {0x00, 0x00, 0x00, 0xFF},  // opaque black
    {0xFF, 0xFF, 0xFF, 0xFF},  // opaque white
    {0x00, 0x00, 0x00, 0x7F},  // 50% transparent black
    {0xFF, 0xFF, 0xFF, 0x7F},  // 50% transparent white
    {0xC5, 0x5C, 0x55, 0xCC},  // alternating bits 1
    {0x55, 0x5C, 0xC5, 0xCC},  // alternating bits 2
    {0x01, 0x23, 0x45, 0x67},  // sequential ascending
    {0xFE, 0xDC, 0xBA, 0x98},  // sequential descending
};

TEST(RgbaImageFormat, RGBAColorConversionForAllFormats) {
  auto testing_formats = GetSupportedFormats();
  for (size_t i = 0; i < testing_formats.size(); ++i) {
    auto format1 = testing_formats[i];
    for (size_t j = i; j < testing_formats.size(); ++j) {
      auto format2 = testing_formats[j];
      for (auto color : kTestColors) {
        auto packed1 = RgbaPack(format1, color);
        auto packed2 = RgbaPack(format2, color);
        ASSERT_TRUE(ComparePacked(format1, packed1, format2, packed2))
            << "Format " << ToString(format1) << " did not match format " << ToString(format2)
            << "  At R: " << (int)color.r << " G: " << (int)color.g << " B: " << (int)color.b
            << " A: " << (int)color.a << "  packed1: " << packed1 << "  packed2: " << packed2;
      }
    }
  }
}

}  // namespace
}  // namespace camera
