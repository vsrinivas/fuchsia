// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image.h"
#include "runner.h"
#include "utils.h"

namespace {

static uint32_t get_next_color() {
  static uint32_t next_color_idx = 0x1;  // skip black
  ZX_ASSERT(next_color_idx <= 0xfff);
  // Map 0xXYZ -> 0xffX0Y0Z0
  uint32_t res = ((next_color_idx & 0xf) << 4) | ((next_color_idx & 0xf0) << 8) |
                 ((next_color_idx & 0xf00) << 12);
  next_color_idx++;
  return res;
}

static uint32_t compute_color(uint8_t alpha, bool premultiplied, bool bg) {
  static uint32_t bg_color = get_next_color();

  uint32_t color = (bg ? bg_color : get_next_color()) | (alpha << 24);
  return premultiplied ? display_test::internal::premultiply_color_channels(color, alpha) : color;
}

}  // namespace

namespace display_test {
namespace internal {

ImageImpl::ImageImpl(Runner* runner, uint32_t width, uint32_t height, bool scalable, uint8_t alpha,
                     bool premultiplied)
    : width_(width),
      height_(height),
      scalable_(scalable),
      fg_color_(compute_color(alpha, premultiplied, false)),
      bg_color_(compute_color(alpha, premultiplied, true)),
      runner_(runner) {
  ZX_ASSERT(width % 2 == 0);

  if (scalable) {
    ZX_ASSERT(width >= kMinScalableImageSize && height >= kMinScalableImageSize);
  }

  runner->display()->ComputeLinearImageStride(
      width_, kFormat, fit::bind_member(this, &ImageImpl::ComputeLinearStrideCallback));
}

void ImageImpl::ComputeLinearStrideCallback(uint32_t stride) {
  ZX_ASSERT(stride);
  stride_ = stride;
  runner_->display()->AllocateVmo(height_ * stride * kBytesPerPixel,
                                  fit::bind_member(this, &ImageImpl::AllocateVmoCallback));
}

void ImageImpl::AllocateVmoCallback(zx_status_t status, zx::vmo vmo) {
  ZX_ASSERT(status == ZX_OK);
  zx_vaddr_t addr;
  uint32_t size = height_ * stride_ * kBytesPerPixel;
  status = zx::vmar::root_self()->map(0, vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr);
  ZX_ASSERT(status == ZX_OK);

  uint32_t* ptr = reinterpret_cast<uint32_t*>(addr);
  for (uint32_t y = 0; y < height_; y++) {
    for (uint32_t x = 0; x < width_; x += 2) {
      uint32_t val = get_pixel(x, y);
      *(ptr + y * stride_ + x) = val;
      *(ptr + y * stride_ + x + 1) = val;
    }
  }
  zx_cache_flush(ptr, size, ZX_CACHE_FLUSH_DATA);

  status = zx::vmar::root_self()->unmap(addr, size);
  ZX_ASSERT(status == ZX_OK);

  fuchsia::hardware::display::ImageConfig config{
      .width = width_,
      .height = height_,
      .pixel_format = kFormat,
  };

  runner_->display()->ImportVmoImage(config, std::move(vmo), 0,
                                     fit::bind_member(this, &ImageImpl::ImportVmoImageCallback));
}

void ImageImpl::ImportVmoImageCallback(zx_status_t status, uint64_t id) {
  ZX_ASSERT(status == ZX_OK);
  id_ = id;
  runner_->OnResourceReady();
}

bool ImageImpl::is_fg_pixel(uint32_t x, uint32_t y) const {
  // If it's a scalable image, simplify the image so that we don't have
  // to care about the exact interpolation done by the hardware.
  if (scalable_) {
    return (x < width_ / 2) ^ (y < height_ / 2);
  }

  // Include a border to ensure that rotations/reflections are distinct
  if (x < 4 || y < 4) {
    return true;
  } else if (x >= width_ - 4 || y >= height_ - 4) {
    return false;
  }

  // Otherwise generate rectangular tilings
  return ((y / 32) % 2) == ((x / 64) % 2);
}

uint32_t ImageImpl::get_pixel(uint32_t x_pos, uint32_t y_pos) const {
  return is_fg_pixel(x_pos, y_pos) ? fg_color_ : bg_color_;
}

const ImageImpl* ImageImpl::GetImageImpl(const Image* image) { return image; }

}  // namespace internal
}  // namespace display_test
