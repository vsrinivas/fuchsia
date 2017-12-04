// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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

  ~FramebufferScanout();

  void FlushRegion(const virtio_gpu_rect_t& rect) override;

 private:
  FramebufferScanout(GpuBitmap surface, int fd);

  int fd_;
};

}  // namespace machina
