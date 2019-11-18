// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>

#include <src/lib/syslog/cpp/logger.h>

#include "gtest/gtest.h"
#include "src/camera/image_writer/hsv_generator.h"
#include "src/camera/image_writer/image_format_rgba.h"

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

// Make an array of values that should be tested, given a specific bit width
// and the knowledge that the bit value will be shifted to the most significant
// position in the byte.
// For example, a bit width of 2 would only need to test the following binary values:
// 00000000
// 01000000
// 10000000
// 11000000
std::vector<uint8_t> GetTestArray(uint8_t bit_width) {
  if (bit_width < 8) {
    size_t num_values = 1 << bit_width;
    std::vector<uint8_t> ret(num_values);
    for (size_t i = 0; i < num_values; ++i) {
      ret[i] = i << (8 - bit_width);
    }
    return ret;
  }
  // for bit_width == 8, only use every 7th number, but include 255
  std::vector<uint8_t> ret;
  for (size_t i = 0; i < 256; i += 7) {
    ret.push_back(i);
  }
  ret.push_back(255);
  return ret;
}

std::array<std::vector<uint8_t>, 9> testing_arrays = {
    GetTestArray(0), GetTestArray(1), GetTestArray(2), GetTestArray(3), GetTestArray(4),
    GetTestArray(5), GetTestArray(6), GetTestArray(7), GetTestArray(8),
};

TEST(RgbaImageFormat, RGBAColorConversionForAllFormats) {
  auto testing_formats = GetSupportedFormats();
  for (size_t i = 0; i < testing_formats.size(); ++i) {
    auto format1 = testing_formats[i];
    for (size_t j = i; j < testing_formats.size(); ++j) {
      auto format2 = testing_formats[j];
      Rgba min_bit_width = Min(BitWidth(format1), BitWidth(format2));
      for (uint8_t r : testing_arrays[min_bit_width.r]) {
        for (uint8_t g : testing_arrays[min_bit_width.g]) {
          for (uint8_t b : testing_arrays[min_bit_width.b]) {
            for (uint8_t a : testing_arrays[min_bit_width.a]) {
              Rgba color{r, g, b, a};
              auto packed1 = RgbaPack(format1, color);
              auto packed2 = RgbaPack(format2, color);
              ASSERT_TRUE(ComparePacked(format1, packed1, format2, packed2))
                  << "Format " << ToString(format1) << " did not match format " << ToString(format2)
                  << "  At R: " << (int)r << " G: " << (int)g << " B: " << (int)b
                  << " A: " << (int)a << "  packed1: " << packed1 << "  packed2: " << packed2;
            }
          }
        }
      }
    }
  }
}

}  // namespace
}  // namespace camera
