// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_
#define GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <virtio/gpu.h>
#include <zircon/types.h>

#include "garnet/lib/machina/gpu_scanout.h"

namespace machina {

// A scanout that renders to a zircon framebuffer device.
class FramebufferScanout : public GpuScanout {
 public:
  // Create a scanout that owns a zircon framebuffer device.
  static zx_status_t Create(const char* path, fbl::unique_ptr<GpuScanout>* out);

  void FlushRegion(const virtio_gpu_rect_t& rect) override;

 private:
  FramebufferScanout(GpuBitmap surface, fbl::unique_fd fd);

  fbl::unique_fd fd_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_FRAMEBUFFER_SCANOUT_H_
