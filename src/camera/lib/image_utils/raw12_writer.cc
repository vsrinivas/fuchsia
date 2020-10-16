// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_utils/raw12_writer.h"

#include <lib/syslog/cpp/macros.h>

#include <array>
#include <cmath>
#include <vector>

namespace camera {

// Helper methods.
std::array<uint8_t, kBytesPerDoublePixel> PixelValuesToDoublePixel(uint16_t first_pixel_val,
                                                                   uint16_t second_pixel_val) {
  return std::array<uint8_t, kBytesPerDoublePixel>(
      {static_cast<uint8_t>(((first_pixel_val & kHighByteMask) >> kHalfByteShift)),
       static_cast<uint8_t>(((second_pixel_val & kHighByteMask) >> kHalfByteShift)),
       static_cast<uint8_t>((((second_pixel_val & kLowHalfByteMask) << kHalfByteShift) |
                             (first_pixel_val & kLowHalfByteMask)))});
}

std::pair<uint16_t, uint16_t> DoublePixelToPixelValues(
    std::array<uint8_t, kBytesPerDoublePixel> double_pixel) {
  return std::pair(
      (double_pixel[0] << kHalfByteShift) | (double_pixel[2] & kLowHalfByteMask),
      (double_pixel[1] << kHalfByteShift) | ((double_pixel[2] & kHighByteMask) >> kHalfByteShift));
}

// RAW12 Writer methods.
fit::result<std::unique_ptr<Raw12Writer>, zx_status_t> Raw12Writer::Create(uint32_t width,
                                                                           uint32_t height) {
  // TODO(nzo): consider if there's a case where odd width/height would be necessary.
  if (!(width > 0 && height > 0)) {
    FX_LOGS(DEBUG) << "Invalid dimensions passed in.";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  const size_t kSize = width * height * kBytesPerDoublePixel;
  return fit::ok(std::make_unique<Raw12Writer>(width, height, kSize));
}

zx_status_t Raw12Writer::Write(zx::vmo* vmo, uint16_t r, uint16_t g, uint16_t b) {
  zx_status_t status = zx::vmo::create(vmo_size(), 0, vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create VMO.";
    return status;
  }

  std::array<uint8_t, kBytesPerDoublePixel> double_pixel_val;

  auto buf = std::vector<uint8_t>(vmo_size(), 0);

  // Value for colors on the first column/row will be zero, increase by constant factor up to
  // maximum value for every column/row after that.
  const uint16_t kGreenStepFactor = (height() == 1) ? 0 : b / (height() - 1);
  const uint16_t kBlueStepFactor = (width() == 1) ? 0 : g / (width() - 1);
  uint16_t green_pixel = 0;
  uint16_t blue_pixel = 0;

  uint32_t row_num = 0;

  for (uint32_t i = 0; i < vmo_size(); i += kBytesPerDoublePixel) {
    if ((i != 0) && (i % (width() * kBytesPerDoublePixel) == 0)) {
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

  status = vmo->write(&buf.front(), 0, buf.size());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to VMO.";
    return status;
  }

  return ZX_OK;
}

}  // namespace camera
