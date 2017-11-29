// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_SCANOUT_H_
#define GARNET_LIB_MACHINA_GPU_SCANOUT_H_

#include <virtio/gpu.h>
#include <zircon/types.h>

#include "third_party/skia/include/core/SkSurface.h"

class GpuResource;

// A scanout represents a display that GPU resources can be rendered to.
class GpuScanout {
 public:
  GpuScanout(sk_sp<SkSurface>&& surface) : surface_(surface) {}

  virtual ~GpuScanout() = default;

  uint32_t width() const { return surface_->width(); }
  uint32_t height() const { return surface_->height(); }

  virtual void FlushRegion(const virtio_gpu_rect_t& rect);

  zx_status_t SetResource(GpuResource* res,
                          const virtio_gpu_set_scanout_t* request);

 private:
  sk_sp<SkSurface> surface_;

  // Scanout parameters.
  GpuResource* resource_;
  SkRect rect_;
};

#endif  // GARNET_LIB_MACHINA_GPU_SCANOUT_H_
