// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_utils/hsv_generator.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/camera/lib/image_utils/image_format_rgba.h"

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
// in the RGBColorConversion test in image_format_test.
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
