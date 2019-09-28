// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../image_writer.h"

#include <zircon/types.h>

#include <array>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "src/camera/drivers/isp/modules/dma-format.h"

namespace camera {
namespace {

// All widths/heights are in double-pixels, i.e. a width of 500 means a row of '500 *
// kBytesPerDoublePixel' bytes.
constexpr uint32_t kWidth = 2;
constexpr uint32_t kHeight = 2;
constexpr size_t kSquareImageSize = kWidth * kHeight * kBytesPerDoublePixel;
constexpr uint32_t kDoubleWidth = kWidth * 2;
constexpr uint32_t kDoubleHeight = kHeight * 2;
constexpr uint32_t kHalfWidth = kWidth / 2;
constexpr uint32_t kHalfHeight = kHeight / 2;
constexpr size_t kLongImageSize = kHalfWidth * kDoubleHeight * kBytesPerDoublePixel;
constexpr size_t kWideImageSize = kDoubleWidth * kHalfHeight * kBytesPerDoublePixel;
const std::array<uint8_t, kSquareImageSize> kSquareImage = {0x80, 0x00, 0x00, 0x80, 0x00, 0x00,
                                                            0xFF, 0x00, 0x0F, 0xFF, 0xFF, 0xFF};
const std::array<uint8_t, kLongImageSize> kLongImage = {0x80, 0x00, 0x00, 0x55, 0x00, 0x05,
                                                        0x80, 0xAA, 0xA0, 0xFF, 0x00, 0x0F};
const std::array<uint8_t, kWideImageSize> kWideImage = {0x80, 0x00, 0x00, 0x80, 0x00, 0x00,
                                                        0x80, 0x00, 0x00, 0x80, 0x00, 0x00};

constexpr uint32_t kExhaustiveWidth = 500;
constexpr uint32_t kExhaustiveHeight = 500;
constexpr uint32_t kExhaustiveImageSize =
    kExhaustiveWidth * kExhaustiveHeight * kBytesPerDoublePixel;

// Padding is in bytes.
constexpr uint32_t kPadding = 1000;

TEST(ImageWriterTest, Constructor) { ImageWriter::Init(kWidth, kHeight, kPixelTypeRaw12); }

TEST(ImageWriterTest, ConstructorCheckFailsWithInvalidPixelType) {
  ASSERT_DEATH(ImageWriter::Init(kWidth, kHeight, camera::DmaFormat::INVALID),
               "Pixel format must be RAW12.");
}
TEST(ImageWriterTest, ConstructorCheckFailsWithZeroWidth) {
  ASSERT_DEATH(ImageWriter::Init(0, kHeight, kPixelTypeRaw12), "Invalid dimensions passed in.");
}
TEST(ImageWriterTest, ConstructorCheckFailsWithZeroHeight) {
  ASSERT_DEATH(ImageWriter::Init(kWidth, 0, kPixelTypeRaw12), "Invalid dimensions passed in.");
}

// Helper method to initialize an ImageWriter and write an image of specified width/height.
zx_status_t ReadImage(std::vector<uint8_t>* buf, uint32_t width, uint32_t height) {
  std::unique_ptr<ImageWriter> image_writer = ImageWriter::Init(width, height, kPixelTypeRaw12);

  zx_status_t status;
  zx::vmo vmo;

  status = image_writer->Write(&vmo);
  vmo.read(&buf->front(), 0, image_writer->VmoSize());

  return status;
}

// Helper method to retrieve a double-pixel byte array from a specific x and y position in an image.
// Width and height provided to calculate scaling factors.
std::array<uint8_t, kBytesPerDoublePixel> GetDoublePixelAtPostion(uint32_t width, uint32_t height,
                                                                  uint32_t x_pos, uint32_t y_pos) {
  if (x_pos >= width || y_pos >= height) {
    return std::array<uint8_t, kBytesPerDoublePixel>();
  }

  const uint16_t kRowStep = kMaxVal / (width - 1);
  const uint16_t kHeightStep = kMaxVal / (height - 1);

  uint16_t blue_pixel = kRowStep * x_pos;
  uint16_t green_pixel = kHeightStep * y_pos;

  return (y_pos % 2 == 0) ? ImageWriter::PixelValuesToDoublePixel(kRedPixel, green_pixel)
                          : ImageWriter::PixelValuesToDoublePixel(green_pixel, blue_pixel);
}

// Helper method to copy a target pixel from one array/vector to the other.
void CopyDoublePixel(uint8_t* target, const uint8_t* source, uint32_t target_index,
                     uint32_t source_index = 0) {
  target[target_index] = source[source_index + 0];
  target[target_index + 1] = source[source_index + 1];
  target[target_index + 2] = source[source_index + 2];
}

TEST(ImageWriterTest, WriteSquareImage) {
  std::vector<uint8_t> buf(kSquareImageSize);
  zx_status_t status = ReadImage(&buf, kWidth, kHeight);
  ASSERT_EQ(status, ZX_OK);

  auto expected_buf = std::vector<uint8_t>(kSquareImage.begin(), kSquareImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(ImageWriterTest, WriteLongImage) {
  std::vector<uint8_t> buf(kLongImageSize);
  zx_status_t status = ReadImage(&buf, kHalfWidth, kDoubleHeight);
  ASSERT_EQ(status, ZX_OK);

  auto expected_buf = std::vector<uint8_t>(kLongImage.begin(), kLongImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(ImageWriterTest, WriteWideImage) {
  std::vector<uint8_t> buf(kWideImageSize);
  zx_status_t status = ReadImage(&buf, kDoubleWidth, kHalfHeight);
  ASSERT_EQ(status, ZX_OK);

  auto expected_buf = std::vector<uint8_t>(kWideImage.begin(), kWideImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(ImageWriterTest, WriteExhaustiveImage) {
  std::vector<uint8_t> buf(kExhaustiveImageSize);
  zx_status_t status = ReadImage(&buf, kExhaustiveWidth, kExhaustiveHeight);
  ASSERT_EQ(status, ZX_OK);

  // Test byte value at the center.
  const uint32_t kCenterWidth = kExhaustiveWidth / 2;
  const uint32_t kCenterHeight = kExhaustiveHeight / 2;
  uint32_t index = (kExhaustiveWidth * kCenterHeight + kCenterWidth) * kBytesPerDoublePixel;

  auto double_pixel =
      std::array<uint8_t, kBytesPerDoublePixel>({buf[index], buf[index + 1], buf[index + 2]});
  std::array<uint8_t, kBytesPerDoublePixel> expected_double_pixel =
      GetDoublePixelAtPostion(kExhaustiveWidth, kExhaustiveHeight, kCenterWidth, kCenterHeight);

  EXPECT_TRUE(expected_double_pixel == double_pixel);

  const size_t kBorderSize = kExhaustiveWidth * kBytesPerDoublePixel;
  std::vector<uint8_t> expected_top_border(kBorderSize);
  std::vector<uint8_t> expected_bottom_border(kBorderSize);
  std::vector<uint8_t> expected_left_border(kBorderSize);
  std::vector<uint8_t> expected_right_border(kBorderSize);
  std::vector<uint8_t> actual_left_border(kBorderSize);
  std::vector<uint8_t> actual_right_border(kBorderSize);
  index = 0;
  for (uint32_t i = 0; i < kExhaustiveWidth; i++) {
    // Test byte values along the top and bottom rows.
    expected_double_pixel = GetDoublePixelAtPostion(kExhaustiveWidth, kExhaustiveHeight, i, 0);
    CopyDoublePixel(&expected_top_border[0], &expected_double_pixel[0], index);

    expected_double_pixel =
        GetDoublePixelAtPostion(kExhaustiveWidth, kExhaustiveHeight, i, kExhaustiveHeight - 1);
    CopyDoublePixel(&expected_bottom_border[0], &expected_double_pixel[0], index);

    // Test byte values along the left and right columns.
    uint32_t first_column_byte_offset = i * kExhaustiveWidth * kBytesPerDoublePixel;
    uint32_t last_column_byte_offset =
        first_column_byte_offset + (kExhaustiveWidth - 1) * kBytesPerDoublePixel;

    expected_double_pixel = GetDoublePixelAtPostion(kExhaustiveWidth, kExhaustiveHeight, 0, i);
    CopyDoublePixel(&expected_left_border[0], &expected_double_pixel[0], index);
    CopyDoublePixel(&actual_left_border[0], &buf[0], index, first_column_byte_offset);

    expected_double_pixel =
        GetDoublePixelAtPostion(kExhaustiveWidth, kExhaustiveHeight, kExhaustiveWidth - 1, i);
    CopyDoublePixel(&expected_right_border[0], &expected_double_pixel[0], index);
    CopyDoublePixel(&actual_right_border[0], &buf[0], index, last_column_byte_offset);

    index += kBytesPerDoublePixel;
  }

  // std::equal needed for the first two comparisons as it automatically factors in buf size.
  EXPECT_TRUE(std::equal(expected_top_border.begin(), expected_top_border.end(), buf.begin()));
  EXPECT_TRUE(
      std::equal(expected_bottom_border.begin(), expected_bottom_border.end(),
                 buf.begin() + kExhaustiveWidth * (kExhaustiveHeight - 1) * kBytesPerDoublePixel));
  EXPECT_TRUE(actual_left_border == expected_left_border);
  EXPECT_TRUE(actual_right_border == expected_right_border);
}

TEST(ImageWriterTest, WriteDoesNotOverwriteBytesPastImage) {
  // Add some padding to the buffer (after the image).
  std::vector<uint8_t> buf(kExhaustiveImageSize + kPadding);
  zx_status_t status = ReadImage(&buf, kExhaustiveWidth, kExhaustiveHeight);
  ASSERT_EQ(status, ZX_OK);

  // Compare it to a buffer of zeroes of equal length to the padding.
  const std::array<uint8_t, kPadding> kExpectedBuf = {};

  // Check that the last byte in the image is not 0.
  EXPECT_NE(0x0, buf[kExhaustiveImageSize - 1]);
  // Compares kExpectedBuf to end of buf + size of kExpectedBuf.
  EXPECT_TRUE(std::equal(kExpectedBuf.begin(), kExpectedBuf.end(), buf.begin() + kExhaustiveImageSize));
}

TEST(ImageWriterTest, PixelValuesToDoublePixelWorksCorrectly) {
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedZeroBuf = {};
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedFullBuf = {0xFF, 0xFF, 0xFF};
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedRandomBuf = {0x3C, 0x09, 0xD1};

  EXPECT_EQ(kExpectedZeroBuf, ImageWriter::PixelValuesToDoublePixel(0x0, 0x0));
  EXPECT_EQ(kExpectedFullBuf, ImageWriter::PixelValuesToDoublePixel(0xFFF, 0xFFF));
  EXPECT_EQ(kExpectedRandomBuf, ImageWriter::PixelValuesToDoublePixel(0x3C1, 0x09D));
}

TEST(ImageWriterTest, DoublePixelToPixelValuesWorksCorrectly) {
  const std::array<uint8_t, kBytesPerDoublePixel> kZeroBuf = {};
  const std::array<uint8_t, kBytesPerDoublePixel> kFullBuf = {0xFF, 0xFF, 0xFF};
  const std::array<uint8_t, kBytesPerDoublePixel> kRandomBuf = {0x3C, 0x09, 0xD1};
  const std::tuple<uint16_t, uint16_t> kExpectedZeroTuple(0x0, 0x0);
  const std::tuple<uint16_t, uint16_t> kExpectedFullTuple(0xFFF, 0xFFF);
  const std::tuple<uint16_t, uint16_t> kExpectedRandomTuple(0x3C1, 0x09D);

  EXPECT_EQ(kExpectedZeroTuple, ImageWriter::DoublePixelToPixelValues(kZeroBuf));
  EXPECT_EQ(kExpectedFullTuple, ImageWriter::DoublePixelToPixelValues(kFullBuf));
  EXPECT_EQ(kExpectedRandomTuple, ImageWriter::DoublePixelToPixelValues(kRandomBuf));
}

}  // namespace
}  // namespace camera
