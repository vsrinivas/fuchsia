// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_writer.h"

#include <zircon/status.h>

#include <array>
#include <memory>
#include <vector>

#include <src/lib/fxl/logging.h>

namespace camera {

std::unique_ptr<ImageWriter> ImageWriter::Init(uint32_t width, uint32_t height,
                                               camera::DmaFormat::PixelType pixel_format) {
  // TODO(nzo): eventually requires a refactor to slot in other formats.
  FXL_CHECK(pixel_format == kPixelTypeRaw12) << "Pixel format must be RAW12.";
  // TODO(nzo): consider if there's a case where odd width/height would be necessary.
  FXL_CHECK(width > 0 && height > 0) << "Invalid dimensions passed in.";

  // TODO(nzo): is there a way to incorporate the height from DmaFormat instead of using a manual
  //            calculation?
  const size_t kSize = width * height * kBytesPerDoublePixel;
  const camera::DmaFormat kDmaFormat = camera::DmaFormat(width, height, pixel_format, false);
  return std::make_unique<ImageWriter>(kDmaFormat, kSize);
}

zx_status_t ImageWriter::Write(zx::vmo* vmo, uint16_t r, uint16_t g, uint16_t b) {
  zx_status_t status = zx::vmo::create(vmo_size_, 0, vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create VMO: %s\n", zx_status_get_string(status);
    return status;
  }

  FillRAW12(vmo, r, g, b);

  return ZX_OK;
}

std::array<uint8_t, kBytesPerDoublePixel> ImageWriter::PixelValuesToDoublePixel(
    uint16_t first_pixel_val, uint16_t second_pixel_val) {
  return std::array<uint8_t, kBytesPerDoublePixel>(
      {static_cast<uint8_t>(((first_pixel_val & kHighByteMask) >> kHalfByteShift)),
       static_cast<uint8_t>(((second_pixel_val & kHighByteMask) >> kHalfByteShift)),
       static_cast<uint8_t>((((second_pixel_val & kLowHalfByteMask) << kHalfByteShift) |
                             (first_pixel_val & kLowHalfByteMask)))});
}

std::pair<uint16_t, uint16_t> ImageWriter::DoublePixelToPixelValues(
    std::array<uint8_t, kBytesPerDoublePixel> double_pixel) {
  return std::pair(
      (double_pixel[0] << kHalfByteShift) | (double_pixel[2] & kLowHalfByteMask),
      (double_pixel[1] << kHalfByteShift) | ((double_pixel[2] & kHighByteMask) >> kHalfByteShift));
}

void ImageWriter::FillRAW12(zx::vmo* vmo, uint16_t r, uint16_t g, uint16_t b) {
  FXL_CHECK(*vmo) << "VMO must have been created.";

  std::array<uint8_t, kBytesPerDoublePixel> double_pixel_val;

  auto buf = std::vector<uint8_t>(vmo_size_, 0);

  // Value for colors on the first column/row will be zero, increase by constant factor up to
  // maximum value for every column/row after that.
  const float_t kGreenStepFactor = (dma_format_.height() == 1) ? 0 : b / (dma_format_.height() - 1);
  const float_t kBlueStepFactor = (dma_format_.width() == 1) ? 0 : g / (dma_format_.width() - 1);
  uint16_t green_pixel = 0;
  uint16_t blue_pixel = 0;

  uint32_t row_num = 0;

  for (uint32_t i = 0; i < vmo_size_; i += kBytesPerDoublePixel) {
    if ((i != 0) && (i % (dma_format_.width() * kBytesPerDoublePixel) == 0)) {
      row_num++;
      green_pixel += kGreenStepFactor;
      blue_pixel = 0;
    }

    double_pixel_val = (row_num % 2 == 0) ? PixelValuesToDoublePixel(r, green_pixel)
                                          : PixelValuesToDoublePixel(green_pixel, blue_pixel);
    buf[i] = double_pixel_val[0];
    buf[i + 1] = double_pixel_val[1];
    buf[i + 2] = double_pixel_val[2];

    blue_pixel += kBlueStepFactor;
  }

  vmo->write(&buf.front(), 0, buf.size());
}

}  // namespace camera
