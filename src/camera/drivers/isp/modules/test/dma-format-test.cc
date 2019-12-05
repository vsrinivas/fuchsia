// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../dma-format.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/image-format/image_format.h>
#include <stdint.h>

#include <climits>  // PAGE_SIZE
#include <cstddef>
#include <cstdint>
#include <utility>

#include <zxtest/zxtest.h>

namespace camera {

namespace {

constexpr uint32_t kTestWidth = 640;
constexpr uint32_t kTestHeight = 480;

void TestPixelType(DmaFormat::PixelType pixel_type) {
  DmaFormat format(kTestWidth, kTestHeight, pixel_type, false);
  // Because of line and page alignment, the size should be >= width * height *
  // bytesperpixel
  EXPECT_GE(format.GetImageSize(), kTestWidth * kTestHeight * format.GetBytesPerPixel());

  // Create the flipped version:
  DmaFormat format_flipped(kTestWidth, kTestHeight, pixel_type, true);
  EXPECT_EQ(format.GetImageSize(), format_flipped.GetImageSize());
  // Line offset should be different, because flipped images start in different
  // locations
  EXPECT_EQ(-format.GetLineOffset(), format_flipped.GetLineOffset());
  EXPECT_EQ(0, format.GetBank0Offset());
  EXPECT_GT(format_flipped.GetBank0Offset(), 0);

  // Planar format stuff:
  if (pixel_type < DmaFormat::PixelType::NV12) {
    EXPECT_EQ(format.GetBaseMode(), pixel_type);
    EXPECT_EQ(format.GetPlaneSelectUv(), 0);
  } else {
    EXPECT_NE(format.GetBaseMode(), pixel_type);
    EXPECT_GT(format.GetPlaneSelectUv(), 0);
  }
}

TEST(DmaFormat, CreateWithoutSysmemRGB32) { TestPixelType(DmaFormat::PixelType::RGB32); }

TEST(DmaFormat, CreateWithoutSysmemA2R10G10B10) {
  TestPixelType(DmaFormat::PixelType::A2R10G10B10);
}

TEST(DmaFormat, CreateWithoutSysmemRGB565) { TestPixelType(DmaFormat::PixelType::RGB565); }

TEST(DmaFormat, CreateWithoutSysmemRGB24) { TestPixelType(DmaFormat::PixelType::RGB24); }

TEST(DmaFormat, CreateWithoutSysmemGEN32) { TestPixelType(DmaFormat::PixelType::GEN32); }

TEST(DmaFormat, CreateWithoutSysmemRAW16) { TestPixelType(DmaFormat::PixelType::RAW16); }

TEST(DmaFormat, CreateWithoutSysmemRAW12) { TestPixelType(DmaFormat::PixelType::RAW12); }

TEST(DmaFormat, CreateWithoutSysmemAYUV) { TestPixelType(DmaFormat::PixelType::AYUV); }

TEST(DmaFormat, CreateWithoutSysmemY410) { TestPixelType(DmaFormat::PixelType::Y410); }

TEST(DmaFormat, CreateWithoutSysmemYUY2) { TestPixelType(DmaFormat::PixelType::YUY2); }

TEST(DmaFormat, CreateWithoutSysmemUYVY) { TestPixelType(DmaFormat::PixelType::UYVY); }

TEST(DmaFormat, CreateWithoutSysmemY210) { TestPixelType(DmaFormat::PixelType::Y210); }

TEST(DmaFormat, CreateWithoutSysmemNV12_YUV) { TestPixelType(DmaFormat::PixelType::NV12_YUV); }

TEST(DmaFormat, CreateWithoutSysmemNV12_YVU) { TestPixelType(DmaFormat::PixelType::NV12_YVU); }

TEST(DmaFormat, CreateWithoutSysmemNV12_GREY) { TestPixelType(DmaFormat::PixelType::NV12_GREY); }

TEST(DmaFormat, CreateWithoutSysmemYV12_YU) { TestPixelType(DmaFormat::PixelType::YV12_YU); }

TEST(DmaFormat, CreateWithoutSysmemYV12_YV) { TestPixelType(DmaFormat::PixelType::YV12_YV); }

TEST(DmaFormat, CreateWithoutSysmemInvalid) {
  // There are 3 invalid formats:
  // NV12 and YV12, because they are only used internally, and INVALID:
  ASSERT_DEATH(([]() { DmaFormat(kTestWidth, kTestHeight, DmaFormat::PixelType::NV12, false); }));
  ASSERT_DEATH(([]() { DmaFormat(kTestWidth, kTestHeight, DmaFormat::PixelType::YV12, false); }));
  ASSERT_DEATH(
      ([]() { DmaFormat(kTestWidth, kTestHeight, DmaFormat::PixelType::INVALID, false); }));
}

void TestSysmemType(fuchsia_sysmem_PixelFormatType pixel_type) {
  fuchsia_sysmem_PixelFormat pixel_format = {
      .type = pixel_type,
      .has_format_modifier = false,
      .format_modifier = {.value = 0},
  };
  fuchsia_sysmem_ImageFormat_2 image_format = {
      .pixel_format = pixel_format,
      .coded_width = kTestWidth,
      .coded_height = kTestHeight,
      .bytes_per_row = ImageFormatStrideBytesPerWidthPixel(&pixel_format) * kTestWidth,
      .display_width = kTestWidth,
      .display_height = kTestHeight,
      .layers = pixel_type == fuchsia_sysmem_PixelFormatType_NV12 ? 2u : 1u,

      .color_space =
          {
              .type = fuchsia_sysmem_ColorSpaceType_SRGB,
          },
      .has_pixel_aspect_ratio = false,
      .pixel_aspect_ratio_width = 1,
      .pixel_aspect_ratio_height = 1};

  DmaFormat format(image_format);
  // Because of line and page alignment, the size should be >= width * height *
  // bytesperpixel
  EXPECT_GE(format.GetImageSize(), kTestWidth * kTestHeight * format.GetBytesPerPixel());

  // Planar format stuff:
  if (pixel_type == fuchsia_sysmem_PixelFormatType_NV12) {
    EXPECT_GT(format.GetPlaneSelectUv(), 0);
  } else {
    EXPECT_EQ(format.GetPlaneSelectUv(), 0);
  }
}

TEST(DmaFormat, CreateWithSysmem_R8G8B8A8) {
  TestSysmemType(fuchsia_sysmem_PixelFormatType_R8G8B8A8);
}

TEST(DmaFormat, CreateWithSysmem_NV12) { TestSysmemType(fuchsia_sysmem_PixelFormatType_NV12); }

TEST(DmaFormat, CreateWithSysmem_YUY2) { TestSysmemType(fuchsia_sysmem_PixelFormatType_YUY2); }

}  // namespace
}  // namespace camera
