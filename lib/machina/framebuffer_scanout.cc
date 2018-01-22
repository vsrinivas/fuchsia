// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/framebuffer_scanout.h"

#include <fcntl.h>
#include <unistd.h>

#include <zircon/device/display.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "garnet/lib/machina/gpu_bitmap.h"

namespace machina {

// Create a scanout that owns a zircon framebuffer device.
zx_status_t FramebufferScanout::Create(const char* path,
                                       fbl::unique_ptr<GpuScanout>* out) {
  // Open framebuffer and get display info.
  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    return ZX_ERR_NOT_FOUND;
  }

  ioctl_display_get_fb_t fb;
  if (ioctl_display_get_fb(fd.get(), &fb) != sizeof(fb)) {
    return ZX_ERR_NOT_FOUND;
  }

  // Map framebuffer VMO.
  uintptr_t fbo;
  size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
  zx_status_t status =
      zx_vmar_map(zx_vmar_root_self(), 0, fb.vmo, 0, size,
                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &fbo);
  if (status != ZX_OK) {
    return status;
  }

  GpuBitmap bitmap(fb.info.width, fb.info.height, fb.info.format,
                   reinterpret_cast<uint8_t*>(fbo));
  fbl::unique_ptr<FramebufferScanout> scanout(
      new FramebufferScanout(fbl::move(bitmap), fbl::move(fd)));
  *out = fbl::move(scanout);
  return ZX_OK;
}

FramebufferScanout::FramebufferScanout(GpuBitmap surface, fbl::unique_fd fd)
    : GpuScanout(fbl::move(surface)), fd_(fbl::move(fd)) {}

void FramebufferScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuScanout::FlushRegion(rect);
  ioctl_display_region_t fb_region = {
      .x = rect.x,
      .y = rect.y,
      .width = rect.width,
      .height = rect.height,
  };
  ioctl_display_flush_fb_region(fd_.get(), &fb_region);
}

}  // namespace machina
