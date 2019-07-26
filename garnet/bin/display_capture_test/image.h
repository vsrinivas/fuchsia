// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_IMAGE_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_IMAGE_H_

#include <inttypes.h>
#include <zircon/pixelformat.h>
#include <lib/zx/vmo.h>

namespace display_test {

// The minimum scalable image size that allows us to check for pixel
// correctness without having to care about the exact scaling algorith.
constexpr uint32_t kMinScalableImageSize = 32;

class Image;

namespace internal {

class Runner;

class ImageImpl {
 public:
  static constexpr zx_pixel_format_t kFormat = ZX_PIXEL_FORMAT_ARGB_8888;
  ImageImpl(Runner* runner, uint32_t width, uint32_t height, bool scalable = false,
            uint8_t alpha = 0xff, bool premultiplied = false);

  uint64_t id() const { return id_; }
  bool is_scalable() const { return scalable_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  zx_pixel_format_t format() const { return kFormat; }
  uint32_t get_pixel(uint32_t x_pos, uint32_t y_pos) const;

  static const ImageImpl* GetImageImpl(const Image* image);

 private:
  static constexpr zx_pixel_format_t kBytesPerPixel = ZX_PIXEL_FORMAT_BYTES(kFormat);

  bool is_fg_pixel(uint32_t x, uint32_t y) const;

  void ComputeLinearStrideCallback(uint32_t stride);
  void AllocateVmoCallback(zx_status_t status, zx::vmo vmo);
  void ImportVmoImageCallback(zx_status_t status, uint64_t id);

  const uint32_t width_;
  const uint32_t height_;
  const bool scalable_;
  const uint32_t fg_color_;
  const uint32_t bg_color_;
  Runner* const runner_;

  uint64_t id_ = 0;
  uint32_t stride_ = 0;
};

}  // namespace internal

class Context;

class Image : internal::ImageImpl {
 public:
  uint32_t width() const { return internal::ImageImpl::width(); }
  uint32_t height() const { return internal::ImageImpl::height(); }

 private:
  Image(internal::Runner* runner, uint32_t width, uint32_t height, bool scalable = false)
      : internal::ImageImpl(runner, width, height, scalable, 0xff, false) {}
  Image(internal::Runner* runner, uint32_t width, uint32_t height, uint8_t alpha,
        bool premultiplied)
      : internal::ImageImpl(runner, width, height, false, alpha, premultiplied) {}
  friend Context;
  friend internal::ImageImpl;
};

}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_IMAGE_H_
