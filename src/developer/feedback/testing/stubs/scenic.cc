// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/scenic.h"

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include <cstdint>

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace stubs {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

struct RGBA {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

}  // namespace

ScreenshotData CreateEmptyScreenshot() {
  ScreenshotData screenshot;
  FX_CHECK(zx::vmo::create(0, 0u, &screenshot.data.vmo) == ZX_OK);
  return screenshot;
}

ScreenshotData CreateCheckerboardScreenshot(const size_t image_dim_in_px) {
  const size_t height = image_dim_in_px;
  const size_t width = image_dim_in_px;
  const size_t block_size = 10;
  const uint8_t black = 0;
  const uint8_t white = 0xff;

  const size_t size_in_bytes = image_dim_in_px * image_dim_in_px * sizeof(RGBA);
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  // We go pixel by pixel, row by row. |y| tracks the row and |x| the column.
  //
  // We compute in which |block_size| x |block_size| block the pixel is to determine the color
  // (black or white). |block_y| tracks the "block" row and |block_x| the "block" column.
  for (size_t y = 0; y < height; ++y) {
    size_t block_y = y / block_size;
    for (size_t x = 0; x < width; ++x) {
      size_t block_x = x / block_size;
      uint8_t block_color = (block_x + block_y) % 2 ? black : white;
      size_t index = y * width + x;
      auto& p = pixels[index];
      p.r = p.g = p.b = block_color;
      p.a = 255;
    }
  }

  ScreenshotData screenshot;
  FX_CHECK(zx::vmo::create(size_in_bytes, 0u, &screenshot.data.vmo) == ZX_OK);
  FX_CHECK(screenshot.data.vmo.write(ptr.get(), 0u, size_in_bytes) == ZX_OK);
  screenshot.data.size = size_in_bytes;
  screenshot.info.height = image_dim_in_px;
  screenshot.info.width = image_dim_in_px;
  screenshot.info.stride = image_dim_in_px * 4u /*4 bytes per pixel*/;
  screenshot.info.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  return screenshot;
}

ScreenshotData CreateNonBGRA8Screenshot() {
  ScreenshotData screenshot = CreateEmptyScreenshot();
  screenshot.info.pixel_format = fuchsia::images::PixelFormat::YUY2;
  return screenshot;
}

void Scenic::TakeScreenshot(TakeScreenshotCallback callback) {
  FX_CHECK(!take_screenshot_responses_.empty())
      << "You need to set up Scenic::TakeScreenshot() responses before testing GetScreenshot() "
         "using set_scenic_responses()";
  TakeScreenshotResponse response = std::move(take_screenshot_responses_[0]);
  take_screenshot_responses_.erase(take_screenshot_responses_.begin());
  callback(std::move(response.screenshot), response.success);
}

void ScenicAlwaysReturnsFalse::TakeScreenshot(TakeScreenshotCallback callback) {
  callback(CreateEmptyScreenshot(), false);
}

void ScenicClosesConnection::TakeScreenshot(TakeScreenshotCallback callback) {
  CloseAllConnections();
}

void ScenicNeverReturns::TakeScreenshot(TakeScreenshotCallback callback) {}

}  // namespace stubs
}  // namespace feedback
