// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/virtual_camera/hsv_generator.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/image-format/image_format.h>

#include <fbl/algorithm.h>
#include <src/lib/syslog/cpp/logger.h>

#include "gtest/gtest.h"

namespace camera {
namespace {

constexpr uint64_t kTestImageWidth = 50;
constexpr uint64_t kTestImageHeight = 80;
constexpr uint64_t kIndicesToVerify = 50;
constexpr uint64_t kFrameIndexToVerify = 123;

static fuchsia_sysmem_PixelFormat ConvertPixelFormatToC(fuchsia::sysmem::PixelFormat format) {
  return {.type = *reinterpret_cast<const fuchsia_sysmem_PixelFormatType*>(&format.type),
          .has_format_modifier = format.has_format_modifier,
          .format_modifier.value = format.format_modifier.value};
}

static fuchsia_sysmem_ColorSpace ConvertColorSpaceToC(fuchsia::sysmem::ColorSpace cs) {
  return {*reinterpret_cast<const fuchsia_sysmem_ColorSpace*>(&cs.type)};
}

static fuchsia_sysmem_ImageFormat_2 ConvertImageFormatToC(fuchsia::sysmem::ImageFormat_2 format) {
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

// Give the number of bits of resolution for the r,g,b,a channels.
zx_status_t RgbaResolution(fuchsia::sysmem::PixelFormatType format, uint8_t* r, uint8_t* g,
                           uint8_t* b, uint8_t* a) {
  switch (format) {
    /// RGB only, 8 bits per each of R/G/B/A sample
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
    /// 32bpp BGRA, 1 plane.  RGB only, 8 bits per each of B/G/R/A sample.
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      *r = 8;
      *b = 8;
      *g = 8;
      *a = 8;
      break;
    /// 24bpp BGR, 1 plane. RGB only, 8 bits per each of B/G/R sample
    case fuchsia::sysmem::PixelFormatType::BGR24:
      *r = 8;
      *b = 8;
      *g = 8;
      *a = 0;
      break;
    /// 16bpp RGB, 1 plane. 5 bits R, 6 bits G, 5 bits B
    case fuchsia::sysmem::PixelFormatType::RGB565:
      *r = 5;
      *b = 5;
      *g = 6;
      *a = 0;
      break;
    /// 8bpp RGB, 1 plane. 3 bits R, 3 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB332:
      *r = 3;
      *b = 2;
      *g = 3;
      *a = 0;
      break;
    /// 8bpp RGB, 1 plane. 2 bits R, 2 bits G, 2 bits B
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      *r = 2;
      *b = 2;
      *g = 2;
      *a = 0;
      break;
    default:
      FX_LOGS(ERROR) << "Unsupported format";
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

// converts the number of bits of resolution (0-8) a field has into a mask
// for that value, assuming it is shifted into the most significant region
// of the byte.
uint8_t BitsResolutionToByteMask(uint8_t resolution) {
  ZX_ASSERT(resolution <= 8);
  return 0xff << (8 - resolution);
}

// Returns the minimum shared bitwise resolution between the two formats: |format1| and |format2|.
// Each of the |r|, |g|, |b|, |a| return contain a bitwise resolution (0-8) for that channel.
void RgbaMinRes(fuchsia::sysmem::PixelFormatType format1, fuchsia::sysmem::PixelFormatType format2,
                uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
  uint8_t r_res1, r_res2, g_res1, g_res2, b_res1, b_res2, a_res1, a_res2;
  ZX_ASSERT(RgbaResolution(format1, &r_res1, &g_res1, &b_res1, &a_res1) == ZX_OK);
  ZX_ASSERT(RgbaResolution(format2, &r_res2, &g_res2, &b_res2, &a_res2) == ZX_OK);
  *r = std::min(r_res1, r_res2);
  *g = std::min(g_res1, g_res2);
  *b = std::min(b_res1, b_res2);
  *a = std::min(a_res1, a_res2);
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

bool ComparePacked(fuchsia::sysmem::PixelFormatType format1, uint32_t value1,
                   fuchsia::sysmem::PixelFormatType format2, uint32_t value2) {
  uint8_t r1, r2, g1, g2, b1, b2, a1, a2;
  uint8_t r_res1, r_res2, g_res1, g_res2, b_res1, b_res2, a_res1, a_res2;
  ZX_ASSERT(RgbaUnpack(format1, &r1, &g1, &b1, &a1, value1) == ZX_OK);
  ZX_ASSERT(RgbaUnpack(format2, &r2, &g2, &b2, &a2, value2) == ZX_OK);
  ZX_ASSERT(RgbaResolution(format1, &r_res1, &g_res1, &b_res1, &a_res1) == ZX_OK);
  ZX_ASSERT(RgbaResolution(format2, &r_res2, &g_res2, &b_res2, &a_res2) == ZX_OK);
  uint8_t rmask = BitsResolutionToByteMask(r_res1) & BitsResolutionToByteMask(r_res2);
  uint8_t gmask = BitsResolutionToByteMask(g_res1) & BitsResolutionToByteMask(g_res2);
  uint8_t bmask = BitsResolutionToByteMask(b_res1) & BitsResolutionToByteMask(b_res2);
  uint8_t amask = BitsResolutionToByteMask(a_res1) & BitsResolutionToByteMask(a_res2);
  EXPECT_EQ((r1 & rmask), (r2 & rmask));
  EXPECT_EQ((g1 & gmask), (g2 & gmask));
  EXPECT_EQ((b1 & bmask), (b2 & bmask));
  EXPECT_EQ((a1 & amask), (a2 & amask));

  if ((r1 & rmask) != (r2 & rmask)) {
    return false;
  }
  if ((g1 & gmask) != (g2 & gmask)) {
    return false;
  }
  if ((b1 & bmask) != (b2 & bmask)) {
    return false;
  }
  if ((a1 & amask) != (a2 & amask)) {
    return false;
  }
  return true;
}

const fuchsia::sysmem::PixelFormatType kTestingFormats[] = {
    fuchsia::sysmem::PixelFormatType::R8G8B8A8, fuchsia::sysmem::PixelFormatType::BGRA32,
    fuchsia::sysmem::PixelFormatType::BGR24,    fuchsia::sysmem::PixelFormatType::RGB565,
    fuchsia::sysmem::PixelFormatType::RGB332,   fuchsia::sysmem::PixelFormatType::RGB2220};

std::string ToString(const fuchsia::sysmem::PixelFormatType& type) {
  switch (type) {
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return "R8G8B8A8";
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return "BGRA32";
    case fuchsia::sysmem::PixelFormatType::BGR24:
      return "BGR24";
    case fuchsia::sysmem::PixelFormatType::RGB565:
      return "RGB565";
    case fuchsia::sysmem::PixelFormatType::RGB332:
      return "RGB332";
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      return "RGB2220";
    default:
      return "";
  }
  return "";
}

TEST(HsvGenerator, RGBAColorConversionForAllFormats) {
  for (size_t i = 0; i < fbl::count_of(kTestingFormats); ++i) {
    auto format1 = kTestingFormats[i];
    for (size_t j = i; j < fbl::count_of(kTestingFormats); ++j) {
      auto format2 = kTestingFormats[j];
      uint8_t r_res, g_res, b_res, a_res;
      RgbaMinRes(format1, format2, &r_res, &g_res, &b_res, &a_res);
      for (uint8_t r : testing_arrays[r_res]) {
        for (uint8_t g : testing_arrays[g_res]) {
          for (uint8_t b : testing_arrays[b_res]) {
            for (uint8_t a : testing_arrays[a_res]) {
              uint32_t packed1, packed2;
              RgbaPack(format1, r, g, b, a, &packed1);
              RgbaPack(format2, r, g, b, a, &packed2);
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
