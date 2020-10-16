// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_utils/raw12_writer.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

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

TEST(Raw12WriterTest, Constructor) { Raw12Writer::Create(kWidth, kHeight); }

TEST(Raw12WriterTest, ConstructorCheckFailsWithZeroWidth) {
  ASSERT_TRUE(Raw12Writer::Create(0, kHeight).is_error());
}
TEST(Raw12WriterTest, ConstructorCheckFailsWithZeroHeight) {
  ASSERT_TRUE(Raw12Writer::Create(kWidth, 0).is_error());
}

// Helper method to initialize a Raw12Writer and write an image of specified width/height.
void ReadTestImage(std::vector<uint8_t>* buf, uint32_t width, uint32_t height) {
  auto result = Raw12Writer::Create(width, height);
  ASSERT_TRUE(result.is_ok());
  auto image_writer = result.take_value();

  zx_status_t status;
  zx::vmo vmo;

  status = image_writer->Write(&vmo, kRedPixel, kMaxVal, kMaxVal);
  ASSERT_EQ(status, ZX_OK);

  status = vmo.read(&buf->front(), 0, image_writer->vmo_size());
  ASSERT_EQ(status, ZX_OK);
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

  return (y_pos % 2 == 0) ? PixelValuesToDoublePixel(kRedPixel, green_pixel)
                          : PixelValuesToDoublePixel(green_pixel, blue_pixel);
}

// Helper method to copy a target pixel from one array/vector to the other.
void CopyDoublePixel(uint8_t* target, const uint8_t* source, uint32_t target_index,
                     uint32_t source_index = 0) {
  target[target_index] = source[source_index + 0];
  target[target_index + 1] = source[source_index + 1];
  target[target_index + 2] = source[source_index + 2];
}

TEST(Raw12WriterTest, WriteSquareImage) {
  std::vector<uint8_t> buf(kSquareImageSize);
  ASSERT_NO_FATAL_FAILURE(ReadTestImage(&buf, kWidth, kHeight));

  auto expected_buf = std::vector<uint8_t>(kSquareImage.begin(), kSquareImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(Raw12WriterTest, WriteLongImage) {
  std::vector<uint8_t> buf(kLongImageSize);
  ASSERT_NO_FATAL_FAILURE(ReadTestImage(&buf, kHalfWidth, kDoubleHeight));

  auto expected_buf = std::vector<uint8_t>(kLongImage.begin(), kLongImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(Raw12WriterTest, WriteWideImage) {
  std::vector<uint8_t> buf(kWideImageSize);
  ASSERT_NO_FATAL_FAILURE(ReadTestImage(&buf, kDoubleWidth, kHalfHeight));

  auto expected_buf = std::vector<uint8_t>(kWideImage.begin(), kWideImage.end());

  EXPECT_TRUE(buf == expected_buf);
}

TEST(Raw12WriterTest, WriteExhaustiveImage) {
  std::vector<uint8_t> buf(kExhaustiveImageSize);
  ASSERT_NO_FATAL_FAILURE(ReadTestImage(&buf, kExhaustiveWidth, kExhaustiveHeight));

  // Test byte value at the center.
  const uint32_t kCenterWidth = kExhaustiveWidth / 2;
  const uint32_t kCenterHeight = kExhaustiveHeight / 2;
  uint32_t index = (kExhaustiveWidth * kCenterHeight + kCenterWidth) * kBytesPerDoublePixel;

  auto double_pixel =
      std::array<uint8_t, kBytesPerDoublePixel>({buf[index], buf[index + 1], buf[index + 2]});
  auto expected_double_pixel =
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

TEST(Raw12WriterTest, WriteDoesNotOverwriteBytesPastImage) {
  // Add some padding to the buffer (after the image).
  std::vector<uint8_t> buf(kExhaustiveImageSize + kPadding);
  ASSERT_NO_FATAL_FAILURE(ReadTestImage(&buf, kExhaustiveWidth, kExhaustiveHeight));

  // Compare it to a buffer of zeroes of equal length to the padding.
  const std::array<uint8_t, kPadding> kExpectedBuf = {};

  // Check that the last byte in the image is not 0.
  EXPECT_NE(0x0, buf[kExhaustiveImageSize - 1]);
  // Compares kExpectedBuf to end of buf + size of kExpectedBuf.
  EXPECT_TRUE(
      std::equal(kExpectedBuf.begin(), kExpectedBuf.end(), buf.begin() + kExhaustiveImageSize));
}

TEST(Raw12WriterTest, PixelValuesToDoublePixelWorksCorrectly) {
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedZeroBuf = {};
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedFullBuf = {0xFF, 0xFF, 0xFF};
  const std::array<uint8_t, kBytesPerDoublePixel> kExpectedRandomBuf = {0x3C, 0x09, 0xD1};

  EXPECT_EQ(kExpectedZeroBuf, PixelValuesToDoublePixel(0x0, 0x0));
  EXPECT_EQ(kExpectedFullBuf, PixelValuesToDoublePixel(0xFFF, 0xFFF));
  EXPECT_EQ(kExpectedRandomBuf, PixelValuesToDoublePixel(0x3C1, 0x09D));
}

TEST(Raw12WriterTest, DoublePixelToPixelValuesWorksCorrectly) {
  const std::array<uint8_t, kBytesPerDoublePixel> kZeroBuf = {};
  const std::array<uint8_t, kBytesPerDoublePixel> kFullBuf = {0xFF, 0xFF, 0xFF};
  const std::array<uint8_t, kBytesPerDoublePixel> kRandomBuf = {0x3C, 0x09, 0xD1};
  const std::pair<uint16_t, uint16_t> kExpectedZeroPair(0x0, 0x0);
  const std::pair<uint16_t, uint16_t> kExpectedFullPair(0xFFF, 0xFFF);
  const std::pair<uint16_t, uint16_t> kExpectedRandomPair(0x3C1, 0x09D);

  EXPECT_EQ(kExpectedZeroPair, DoublePixelToPixelValues(kZeroBuf));
  EXPECT_EQ(kExpectedFullPair, DoublePixelToPixelValues(kFullBuf));
  EXPECT_EQ(kExpectedRandomPair, DoublePixelToPixelValues(kRandomBuf));
}

}  // namespace
}  // namespace camera
