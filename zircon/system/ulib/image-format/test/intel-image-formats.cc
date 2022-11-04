// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format/image_format.h>
#include <lib/sysmem-version/sysmem-version.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "fidl/fuchsia.sysmem/cpp/wire.h"
#include "fidl/fuchsia.sysmem2/cpp/wire.h"
#include "fuchsia/sysmem/c/fidl.h"

namespace sysmem_v1 = fuchsia_sysmem;
namespace sysmem_v2 = fuchsia_sysmem2;

TEST(ImageFormat, IntelYTiledFormat_V2) {
  sysmem_v2::PixelFormat pixel_format;
  pixel_format.type().emplace(sysmem_v2::PixelFormatType::kNv12);
  pixel_format.format_modifier_value().emplace(sysmem_v2::kFormatModifierIntelI915YTiled);
  sysmem_v2::ImageFormatConstraints constraints;
  constraints.pixel_format().emplace(std::move(pixel_format));
  constraints.min_coded_width().emplace(128u);
  constraints.min_coded_height().emplace(32u);

  auto image_format_result = ImageConstraintsToFormat(constraints, 3440u, 1440u);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 27u;
  constexpr uint32_t kYPlaneHeightInTiles = 45u;
  constexpr uint32_t kUVPlaneWidthInTiles = 27u;
  constexpr uint32_t kUVPlaneHeightInTiles = 23u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelYTiledFormat_V2_wire) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat pixel_format(allocator);
  pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kNv12);
  pixel_format.set_format_modifier_value(allocator,
                                         sysmem_v2::wire::kFormatModifierIntelI915YTiled);
  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, pixel_format);
  constraints.set_min_coded_width(128u);
  constraints.set_min_coded_height(32u);

  auto image_format_result = ImageConstraintsToFormat(allocator, constraints, 3440u, 1440u);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 27u;
  constexpr uint32_t kYPlaneHeightInTiles = 45u;
  constexpr uint32_t kUVPlaneWidthInTiles = 27u;
  constexpr uint32_t kUVPlaneHeightInTiles = 23u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelYTiledFormat_V1_wire) {
  sysmem_v1::wire::PixelFormat format = {
      .type = sysmem_v1::wire::PixelFormatType::kNv12,
      .has_format_modifier = true,
      .format_modifier =
          {
              .value = sysmem_v1::wire::kFormatModifierIntelI915YTiled,
          },
  };

  sysmem_v1::wire::ImageFormatConstraints constraints = {
      .pixel_format = format,
      .min_coded_width = 128u,
      .max_coded_width = 1920u,
      .min_coded_height = 32u,
      .max_coded_height = 1080u,
      .max_bytes_per_row = 0u,
      .bytes_per_row_divisor = 0u,
  };

  auto optional_format = ImageConstraintsToFormat(constraints, 1920u, 1080u);
  EXPECT_TRUE(optional_format);
  auto& image_format = optional_format.value();

  constexpr uint32_t kTileSize = 4096u;
  constexpr uint32_t kBytesPerRowPerTile = 128u;

  constexpr uint32_t kYPlaneWidthInTiles = 15u;
  constexpr uint32_t kYPlaneHeightInTiles = 34u;
  constexpr uint32_t kUVPlaneWidthInTiles = 15u;
  constexpr uint32_t kUVPlaneHeightInTiles = 17u;

  constexpr uint32_t kYPlaneSize = kYPlaneWidthInTiles * kYPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kUVPlaneSize = kUVPlaneWidthInTiles * kUVPlaneHeightInTiles * kTileSize;
  constexpr uint32_t kTotalSize = kYPlaneSize + kUVPlaneSize;

  EXPECT_EQ(kTotalSize, ImageFormatImageSize(image_format));

  uint64_t y_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 0u, &y_plane_byte_offset));
  EXPECT_EQ(0u, y_plane_byte_offset);

  uint64_t uv_plane_byte_offset;
  EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, 1u, &uv_plane_byte_offset));
  EXPECT_EQ(kYPlaneSize, uv_plane_byte_offset);

  uint32_t y_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0u, &y_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kYPlaneWidthInTiles, y_plane_row_stride);

  uint32_t uv_plane_row_stride;
  EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 1u, &uv_plane_row_stride));
  EXPECT_EQ(kBytesPerRowPerTile * kUVPlaneWidthInTiles, uv_plane_row_stride);
}

TEST(ImageFormat, IntelCcsFormats_V1_wire) {
  for (auto& format_modifier : {
           sysmem_v1::wire::kFormatModifierIntelI915YTiledCcs,
           sysmem_v1::wire::kFormatModifierIntelI915YfTiledCcs,
       }) {
    sysmem_v1::wire::PixelFormat format = {
        .type = sysmem_v1::wire::PixelFormatType::kBgra32,
        .has_format_modifier = true,
        .format_modifier =
            {
                .value = format_modifier,
            },
    };

    sysmem_v1::wire::ImageFormatConstraints constraints = {
        .pixel_format = format,
        .min_coded_width = 12,
        .max_coded_width = 100,
        .min_coded_height = 12,
        .max_coded_height = 100,
        .max_bytes_per_row = 100000,
        .bytes_per_row_divisor = 4 * 8,
    };

    auto optional_format = ImageConstraintsToFormat(constraints, 64, 63);
    EXPECT_TRUE(optional_format);
    auto& image_format = optional_format.value();

    constexpr uint32_t kWidthInTiles = 2;
    constexpr uint32_t kHeightInTiles = 2;
    constexpr uint32_t kTileSize = 4096;
    constexpr uint32_t kMainPlaneSize = kWidthInTiles * kHeightInTiles * kTileSize;
    constexpr uint32_t kCcsWidthInTiles = 1;
    constexpr uint32_t kCcsHeightInTiles = 1;
    constexpr uint32_t kCcsPlane = 3;
    EXPECT_EQ(kMainPlaneSize + kCcsWidthInTiles * kCcsHeightInTiles * kTileSize,
              ImageFormatImageSize(image_format));
    uint64_t ccs_byte_offset;
    EXPECT_TRUE(ImageFormatPlaneByteOffset(image_format, kCcsPlane, &ccs_byte_offset));
    EXPECT_EQ(kMainPlaneSize, ccs_byte_offset);

    uint32_t main_plane_row_stride;
    EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, 0, &main_plane_row_stride));
    EXPECT_EQ(128u * kWidthInTiles, main_plane_row_stride);
    uint32_t ccs_row_stride;
    EXPECT_TRUE(ImageFormatPlaneRowBytes(image_format, kCcsPlane, &ccs_row_stride));
    EXPECT_EQ(ccs_row_stride, 128u * kCcsWidthInTiles);
  }
}

TEST(ImageFormat, IntelYTiledFormat_V2BytesPerRowDivisor) {
  fidl::Arena allocator;
  sysmem_v2::wire::PixelFormat pixel_format(allocator);
  pixel_format.set_type(sysmem_v2::wire::PixelFormatType::kBgra32);
  pixel_format.set_format_modifier_value(allocator,
                                         sysmem_v2::wire::kFormatModifierIntelI915YTiled);
  sysmem_v2::wire::ImageFormatConstraints constraints(allocator);
  constraints.set_pixel_format(allocator, std::move(pixel_format));
  constraints.set_min_coded_width(128u);
  constraints.set_min_coded_height(32u);
  constraints.set_bytes_per_row_divisor(512u);

  constexpr uint32_t kImageWidth = 540u / 4;
  constexpr uint32_t kImageHeight = 140u;
  auto image_format_result =
      ImageConstraintsToFormat(allocator, constraints, kImageWidth, kImageHeight);
  EXPECT_TRUE(image_format_result.is_ok());
  auto image_format = image_format_result.take_value();

  EXPECT_EQ(512u * 2, image_format.bytes_per_row());
  constexpr uint32_t kYTileByteWidth = 128u;
  constexpr uint32_t kYTileHeight = 32u;
  {
    constexpr uint32_t kWidthInTiles = fbl::round_up(512u * 2, kYTileByteWidth) / kYTileByteWidth;
    constexpr uint32_t kHeightInTiles = fbl::round_up(kImageHeight, kYTileHeight) / kYTileHeight;
    constexpr uint32_t kTileSize = 4096;
    constexpr uint32_t kPlaneSize = kWidthInTiles * kHeightInTiles * kTileSize;

    EXPECT_EQ(kPlaneSize, ImageFormatImageSize(image_format));
  }

  // Check that increasing the bytes per row increases the calculated image size.
  image_format.set_bytes_per_row(512u * 3);
  {
    constexpr uint32_t kWidthInTiles = fbl::round_up(512u * 3, kYTileByteWidth) / kYTileByteWidth;
    constexpr uint32_t kHeightInTiles = fbl::round_up(kImageHeight, kYTileHeight) / kYTileHeight;
    constexpr uint32_t kTileSize = 4096;
    constexpr uint32_t kPlaneSize = kWidthInTiles * kHeightInTiles * kTileSize;

    EXPECT_EQ(kPlaneSize, ImageFormatImageSize(image_format));
  }
}
