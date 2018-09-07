// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/framebuffer_scanout.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <lib/framebuffer/framebuffer.h>
#include <lib/fxl/logging.h>
#include <lib/images/cpp/images.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

static constexpr fuchsia::images::PixelFormat kTargetPixelFormat =
    fuchsia::images::PixelFormat::BGRA_8;
static const size_t kTargetPixelFormatSize =
    images::BitsPerPixel(kTargetPixelFormat) / CHAR_BIT;

// NOTE(abdulla): These functions are lightly modified versions of the same
// functions in the Zircon GFX library.

static void argb8888_to_rgb565(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 2, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint16_t out = (in >> 3) & 0x1f;   // b
    out |= ((in >> 10) & 0x3f) << 5;   // g
    out |= ((in >> 19) & 0x1f) << 11;  // r
    *reinterpret_cast<uint16_t*>(dst) = out;
  }
}

static void argb8888_to_rgb332(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint8_t out = (in >> 6) & 0x3;   // b
    out |= ((in >> 13) & 0x7) << 2;  // g
    out |= ((in >> 21) & 0x7) << 5;  // r
    *dst = out;
  }
}

static void argb8888_to_rgb2220(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint8_t out = ((in >> 6) & 0x3) << 2;  // b
    out |= ((in >> 14) & 0x3) << 4;        // g
    out |= ((in >> 22) & 0x3) << 6;        // r
    *dst = out;
  }
}

static void argb8888_to_luma(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint32_t b = (in & 0xff) * 74;
    uint32_t g = ((in >> 8) & 0xff) * 732;
    uint32_t r = ((in >> 16) & 0xff) * 218;
    uint32_t intensity = r + b + g;
    *dst = (intensity >> 10) & 0xff;
  }
}

static void copy(uint8_t* dst, const uint8_t* src, size_t size,
                 zx_pixel_format_t format) {
  switch (format) {
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
      return static_cast<void>(memcpy(dst, src, size));
    case ZX_PIXEL_FORMAT_RGB_565:
      return argb8888_to_rgb565(dst, src, size);
    case ZX_PIXEL_FORMAT_RGB_332:
      return argb8888_to_rgb332(dst, src, size);
    case ZX_PIXEL_FORMAT_RGB_2220:
      return argb8888_to_rgb2220(dst, src, size);
    case ZX_PIXEL_FORMAT_GRAY_8:
      return argb8888_to_luma(dst, src, size);
    default:
      ZX_DEBUG_ASSERT(false);
  }
}

namespace machina {

// Create a scanout that owns a zircon framebuffer device.
zx_status_t FramebufferScanout::Create(
    GpuScanout* scanout, std::unique_ptr<FramebufferScanout>* out) {
  *out = nullptr;

  const char* err;
  zx_status_t status = fb_bind(true, &err);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to bind to framebuffer: " << err << "\n";
    return status;
  }
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  zx_pixel_format_t format;

  fb_get_config(&width, &height, &stride, &format);

  // Map framebuffer VMO.
  uintptr_t fbo;
  size_t size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;
  status =
      zx_vmar_map_old(zx_vmar_root_self(), 0, fb_get_single_buffer(), 0, size,
                      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &fbo);
  if (status != ZX_OK) {
    return status;
  }
  auto buf = reinterpret_cast<uint8_t*>(fbo);
  memset(buf, 0, size);
  zx_cache_flush(buf, size, ZX_CACHE_FLUSH_DATA);

  // Create a compatible buffer if necessary.
  zx::vmo compatible_vmo;
  size_t compatible_vmo_size = 0;
  uintptr_t compatible_vmo_data = 0;
  bool direct_rendering_ok =
      format == ZX_PIXEL_FORMAT_ARGB_8888 || format == ZX_PIXEL_FORMAT_RGB_x888;
  if (direct_rendering_ok) {
    FXL_LOG(INFO) << "Framebuffer pixel format compatible with scanout - "
                     "direct rendering will occur";
  } else {
    FXL_LOG(WARNING) << "Framebuffer pixel format not compatible with scanout "
                        "- conversion will occur";
    compatible_vmo_size = width * height * kTargetPixelFormatSize;
    zx_status_t status =
        zx::vmo::create(compatible_vmo_size, 0, &compatible_vmo);
    FXL_CHECK(status == ZX_OK)
        << "Failed to create guest-compatible VMO " << status;
    status = zx::vmar::root_self()->map(
        0, compatible_vmo, 0, compatible_vmo_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &compatible_vmo_data);
    FXL_CHECK(status == ZX_OK)
        << "Failed to map guest-compatible VMO " << status;
  }

  std::unique_ptr<FramebufferScanout> fbscanout(new FramebufferScanout());
  fbscanout->scanout_ = scanout;
  fbscanout->framebuffer_width_ = width;
  fbscanout->framebuffer_height_ = height;
  fbscanout->framebuffer_linear_stride_px_ = stride;
  fbscanout->framebuffer_format_ = format;
  fbscanout->buf_ = buf;
  fbscanout->direct_rendering_ok_ = direct_rendering_ok;
  fbscanout->compatible_vmo_ = std::move(compatible_vmo);
  fbscanout->compatible_vmo_size_ = compatible_vmo_size;
  fbscanout->compatible_vmo_data_ = compatible_vmo_data;
  if (direct_rendering_ok) {
    scanout->SetFlushTarget(zx::vmo(fb_get_single_buffer()), size, width,
                            height, stride * kTargetPixelFormatSize);
  } else {
    zx::vmo scanout_vmo;
    fbscanout->compatible_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &scanout_vmo);
    scanout->SetFlushTarget(std::move(scanout_vmo), compatible_vmo_size, width,
                            height, width * kTargetPixelFormatSize);
  }
  scanout->SetFlushHandler(
      fit::bind_member(fbscanout.get(), &FramebufferScanout::OnFlush));

  *out = std::move(fbscanout);
  return ZX_OK;
}

FramebufferScanout::~FramebufferScanout() { fb_release(); }

void FramebufferScanout::OnFlush(virtio_gpu_rect_t rect) {
  if (!direct_rendering_ok_) {
    for (uint32_t row = rect.y; row < rect.y + rect.height; ++row) {
      auto copy_dest = buf_ + (row * framebuffer_linear_stride_px_ + rect.x) *
                                  ZX_PIXEL_FORMAT_BYTES(framebuffer_format_);
      auto copy_source =
          reinterpret_cast<uint8_t*>(compatible_vmo_data_) +
          (row * framebuffer_width_ + rect.x) * kTargetPixelFormatSize;
      size_t copy_source_size = rect.width * kTargetPixelFormatSize;
      copy(copy_dest, copy_source, copy_source_size, framebuffer_format_);
    }
  } else {
    for (uint32_t row = rect.y; row < rect.y + rect.height; ++row) {
      uint8_t* ptr = buf_ + (row * framebuffer_linear_stride_px_ + rect.x) *
                                ZX_PIXEL_FORMAT_BYTES(framebuffer_format_);
      zx_cache_flush(ptr,
                     rect.width * ZX_PIXEL_FORMAT_BYTES(framebuffer_format_),
                     ZX_CACHE_FLUSH_DATA);
    }
  }
}

}  // namespace machina
