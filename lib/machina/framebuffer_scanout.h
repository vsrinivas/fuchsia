// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_
#define GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <virtio/gpu.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include "garnet/lib/machina/gpu_scanout.h"

namespace machina {

// A scanout that renders to a zircon framebuffer device.
class FramebufferScanout {
 public:
  ~FramebufferScanout();

  // Create a zircon framebuffer device and set it as the target for scanout.
  static zx_status_t Create(GpuScanout* scanout,
                            std::unique_ptr<FramebufferScanout>* out);
  void OnFlush(virtio_gpu_rect_t rect);

 private:
  FramebufferScanout() = default;
  GpuScanout* scanout_;
  uint32_t framebuffer_width_;
  uint32_t framebuffer_height_;
  uint32_t framebuffer_linear_stride_px_;
  zx_pixel_format_t framebuffer_format_;
  uint8_t* buf_;
  bool direct_rendering_ok_;
  zx::vmo compatible_vmo_;
  size_t compatible_vmo_size_;
  uintptr_t compatible_vmo_data_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_
