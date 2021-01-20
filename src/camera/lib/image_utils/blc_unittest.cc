// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_utils/blc.h"

#include <array>

#include <gtest/gtest.h>

#include "src/camera/lib/image_utils/raw12_writer.h"

namespace camera {
namespace {

constexpr uint32_t kWidth = 500;
constexpr uint32_t kHeight = 500;
constexpr uint16_t kRedVal = 500;
constexpr uint16_t kGreenVal = 500;
constexpr uint16_t kBlueVal = 500;

TEST(BlcTest, BlcRaw12WorksCorrectly) {
  auto result = Raw12Writer::Create(kWidth, kHeight);
  ASSERT_TRUE(result.is_ok());
  auto image_writer = result.take_value();

  zx::vmo vmo;
  std::vector<uint8_t> buf(kWidth * kHeight * kBytesPerDoublePixel);

  const uint32_t kAvg = kGreenVal / 2;
  // Gb starts one line ahead and so will average out to be one value greater than Gr and B.
  const BlcResult kExpectedResult = {kRedVal, kAvg - 1, kAvg, kAvg - 1};

  zx_status_t status = image_writer->Write(&vmo, kRedVal, kGreenVal, kBlueVal);
  ASSERT_EQ(status, ZX_OK);

  const BlcResult kResult = BlcRaw12(&vmo, kWidth, kHeight, kBytesPerDoublePixel);
  ASSERT_EQ(kExpectedResult, kResult);
}

TEST(BlcTest, BlcRaw12RedAveragesCorrectly) {
  constexpr uint8_t kRedTestVal = 0x80;
  constexpr uint32_t kRedTestWidth = 2;
  constexpr uint32_t kRedTestHeight = 4;
  constexpr size_t kRedTestImageSize = kRedTestWidth * kRedTestHeight * kBytesPerDoublePixel;
  std::array<uint8_t, kRedTestImageSize> kRedTestImage = {
      0x00,        0x00, 0x00, 0x00,        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      kRedTestVal, 0x00, 0x00, kRedTestVal, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  zx_status_t status;
  zx::vmo vmo;

  status = zx::vmo::create(kRedTestImageSize, 0, &vmo);
  ASSERT_EQ(status, ZX_OK);
  status = vmo.write(kRedTestImage.data(), 0, kRedTestImageSize);
  ASSERT_EQ(status, ZX_OK);

  // 0x80 must be converted into a double-pixel before averaging; 0x800 in this case.
  const uint16_t kRedAvg = (kRedTestVal << 4) / 2;
  const BlcResult kExpectedResult = {kRedAvg, 0x0, 0x0, 0x0};

  const BlcResult kResult = BlcRaw12(&vmo, kRedTestWidth, kRedTestHeight, kBytesPerDoublePixel);
  ASSERT_EQ(kExpectedResult, kResult);
}

}  // namespace
}  // namespace camera
