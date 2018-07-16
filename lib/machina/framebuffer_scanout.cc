// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/framebuffer_scanout.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <lib/framebuffer/framebuffer.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "garnet/lib/machina/gpu_bitmap.h"
#include "lib/fxl/logging.h"

namespace machina {

// Create a scanout that owns a zircon framebuffer device.
zx_status_t FramebufferScanout::Create(fbl::unique_ptr<GpuScanout>* out) {
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

  memset(reinterpret_cast<void*>(fbo), 0, size);

  GpuBitmap bitmap(width, height, stride, format,
                   reinterpret_cast<uint8_t*>(fbo));
  fbl::unique_ptr<FramebufferScanout> scanout(new FramebufferScanout(
      std::move(bitmap), reinterpret_cast<uint8_t*>(fbo)));

  GpuRect rect = {
      .x = 0,
      .y = 0,
      .width = width,
      .height = height,
  };
  scanout->InvalidateRegion(rect);

  *out = std::move(scanout);
  return ZX_OK;
}

FramebufferScanout::FramebufferScanout(GpuBitmap surface, uint8_t* buf)
    : GpuScanout(std::move(surface)), buf_(buf) {}

FramebufferScanout::~FramebufferScanout() { fb_release(); }

void FramebufferScanout::SetResource(GpuResource* res,
                                     const virtio_gpu_set_scanout_t* request) {
  if (res == nullptr) {
    uint32_t size = height() * stride() * pixelsize();
    memset(buf_, 0, size);
    zx_cache_flush(buf_, size, ZX_CACHE_FLUSH_DATA);
  }
  GpuScanout::SetResource(res, request);
}

void FramebufferScanout::InvalidateRegion(const GpuRect& rect) {
  for (unsigned i = rect.y; i < rect.y + rect.height; i++) {
    uint8_t* ptr = buf_ + (i * stride() + rect.x) * pixelsize();
    zx_cache_flush(ptr, rect.width * pixelsize(), ZX_CACHE_FLUSH_DATA);
  }
}

}  // namespace machina
