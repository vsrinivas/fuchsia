// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/virtual_camera/hsv_generator.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>

#include <fbl/algorithm.h>
#include <src/lib/syslog/cpp/logger.h>

#include "gtest/gtest.h"
#include "src/camera/drivers/virtual_camera/image_format_rgba.h"

namespace camera {
namespace {

constexpr uint64_t kTestImageWidth = 50;
constexpr uint64_t kTestImageHeight = 80;
constexpr uint64_t kIndicesToVerify = 50;
constexpr uint64_t kFrameIndexToVerify = 123;

// Make an ImageFormat_2 struct with default values except for width, height and format.
fuchsia::sysmem::ImageFormat_2 MakeImageFormat(uint32_t width, uint32_t height,
                                               fuchsia::sysmem::PixelFormatType format) {
  fuchsia::sysmem::PixelFormat pixel_format = {.type = format, .has_format_modifier = false};
  fuchsia_sysmem_PixelFormat pixel_format_c = ConvertPixelFormatToC(pixel_format);
  uint32_t bytes_per_row = ImageFormatStrideBytesPerWidthPixel(&pixel_format_c) * width;

  fuchsia::sysmem::ImageFormat_2 ret = {
      .pixel_format = {.type = format, .has_format_modifier = false},
      .coded_width = width,
      .coded_height = height,
      .bytes_per_row = bytes_per_row,
      .display_width = width,
      .display_height = width,
      .layers = 1,
      .color_space = {.type = fuchsia::sysmem::ColorSpaceType::REC601_PAL},
  };
  return ret;
}

bool ComparePacked(fuchsia::sysmem::PixelFormatType format1, uint32_t value1,
                   fuchsia::sysmem::PixelFormatType format2, uint32_t value2) {
  Rgba unpacked1 = RgbaUnpack(format1, value1);
  Rgba unpacked2 = RgbaUnpack(format2, value2);
  // Since the formats may have different resolutions, mask the unpacked values
  // so we only compare bits that both values share.
  Rgba min_mask = BitWidthToByteMask(Min(BitWidth(format1), BitWidth(format2)));
  return (unpacked1 & min_mask) == (unpacked2 & min_mask);
}

std::vector<uint8_t> GetTestArray(uint8_t resolution) {
  if (resolution < 8) {
    size_t num_values = 1 << resolution;
    std::vector<uint8_t> ret(num_values);
    for (size_t i = 0; i < num_values; ++i) {
      ret[i] = i << (8 - resolution);
    }
    return ret;
  }
  // for resolution == 8, only use every 7th number, but include 255
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

TEST(HsvGenerator, RGBAColorConversionForAllFormats) {
  auto testing_formats = GetSupportedFormats();
  for (size_t i = 0; i < testing_formats.size(); ++i) {
    auto format1 = testing_formats[i];
    for (size_t j = i; j < testing_formats.size(); ++j) {
      auto format2 = testing_formats[j];
      auto min_resolution = RgbaMinRes(format1, format2);
      for (uint8_t r : testing_arrays[min_resolution.r]) {
        for (uint8_t g : testing_arrays[min_resolution.g]) {
          for (uint8_t b : testing_arrays[min_resolution.b]) {
            for (uint8_t a : testing_arrays[min_resolution.a]) {
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

fzl::OwnedVmoMapper GenerateImage(const fuchsia::sysmem::ImageFormat_2 format, uint64_t index) {
  fzl::OwnedVmoMapper buffer;
  auto format_c = ConvertImageFormatToC(format);
  EXPECT_EQ(ZX_OK, buffer.CreateAndMap(ImageFormatImageSize(&format_c), "buff"));
  EXPECT_EQ(ZX_OK, HsvGenerator(buffer.start(), format, index));
  return buffer;
}

// test that the image is being generated
// Test that it is changing between indexes
TEST(HsvGeneratorTest, ImageGenerated) {
  auto format1 =
      MakeImageFormat(kTestImageWidth, kTestImageHeight, fuchsia::sysmem::PixelFormatType::BGRA32);
  auto buffer = GenerateImage(format1, 0);
  auto pixels = reinterpret_cast<uint32_t*>(buffer.start());
  for (uint64_t i = 0; i < kTestImageWidth; ++i) {
    EXPECT_EQ(pixels[i], pixels[0]);  // same color
    EXPECT_NE(pixels[i], 0u);         // non-zero image
  }
}

TEST(HsvGeneratorTest, ImageChanges) {
  auto format1 =
      MakeImageFormat(kTestImageWidth, kTestImageHeight, fuchsia::sysmem::PixelFormatType::BGRA32);
  uint32_t first_pixel = 0;
  for (size_t i = 0; i < kIndicesToVerify; ++i) {
    auto buffer = GenerateImage(format1, i);
    EXPECT_NE(first_pixel, reinterpret_cast<uint32_t*>(buffer.start())[0]);
    first_pixel = reinterpret_cast<uint32_t*>(buffer.start())[0];
  }
}

// Test that the first rgb pixel is the same between formats
// This is just a sample format conversion.  Extensive format conversion is done
// in the RGBColorConversion test, above.
TEST(HsvGeneratorTest, FormatsAreSameColor) {
  auto format1 =
      MakeImageFormat(kTestImageWidth, kTestImageHeight, fuchsia::sysmem::PixelFormatType::BGRA32);
  auto buffer = GenerateImage(format1, kFrameIndexToVerify);
  auto bgra_colors = reinterpret_cast<uint8_t*>(buffer.start());
  auto format2 = MakeImageFormat(kTestImageWidth, kTestImageHeight,
                                 fuchsia::sysmem::PixelFormatType::R8G8B8A8);
  auto buffer2 = GenerateImage(format2, kFrameIndexToVerify);
  auto rgba_colors = reinterpret_cast<uint8_t*>(buffer2.start());
  EXPECT_EQ(bgra_colors[3], rgba_colors[1]);
  EXPECT_EQ(bgra_colors[2], rgba_colors[2]);
  EXPECT_EQ(bgra_colors[1], rgba_colors[3]);
  EXPECT_EQ(bgra_colors[0], rgba_colors[0]);
}

}  // namespace
}  // namespace camera
