// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/images/cpp/images.h"

#include <zircon/assert.h>

namespace images {

size_t BitsPerPixel(const fuchsia::images::PixelFormat& pixel_format) {
  switch (pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      return 4u * 8u;
    case fuchsia::images::PixelFormat::YUY2:
      return 2u * 8u;
    case fuchsia::images::PixelFormat::NV12:
      return 12;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format));
  return 0;
}

size_t MaxSampleAlignment(const fuchsia::images::PixelFormat& pixel_format) {
  switch (pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      return 4u;
    case fuchsia::images::PixelFormat::YUY2:
      return 2u;
    case fuchsia::images::PixelFormat::NV12:
      // In the sense that line stride "must preserve pixel alignment", which is
      // what MaxSampleAlignment() is used for, NV12 ~~has (very roughly
      // speaking) a pixel alignment of 2, ~~because the width of the Y plane in
      // this implementation must be even, and ~because the interleaved UV data
      // after the planar Y data is 2 bytes per sample, so we may as well
      // require UV samples to remain aligned UV line to UV line.
      return 2u;
  }
  ZX_PANIC("Unknown Pixel Format: %d", static_cast<int>(pixel_format));
  return 0;
}

size_t ImageSize(const fuchsia::images::ImageInfo& image_info) {
  ZX_DEBUG_ASSERT(image_info.tiling == fuchsia::images::Tiling::LINEAR);
  switch (image_info.pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
    case fuchsia::images::PixelFormat::YUY2:
      return image_info.height * image_info.stride;
    case fuchsia::images::PixelFormat::NV12:
      return image_info.height * image_info.stride * 3 / 2;
  }
  ZX_PANIC("Unknown Pixel Format: %d",
           static_cast<int>(image_info.pixel_format));
  return 0;
}

}  // namespace images
