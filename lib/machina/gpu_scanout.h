// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_SCANOUT_H_
#define GARNET_LIB_MACHINA_GPU_SCANOUT_H_

#include <virtio/gpu.h>
#include <zircon/types.h>

#include "garnet/lib/machina/gpu_bitmap.h"

namespace machina {

class GpuResource;

// A scanout represents a display that GPU resources can be rendered to.
class GpuScanout {
 public:
  GpuScanout() = default;
  GpuScanout(GpuBitmap surface) : surface_(fbl::move(surface)) {}

  virtual ~GpuScanout() = default;

  void SetBitmap(GpuBitmap bitmap) { surface_ = fbl::move(bitmap); }

  uint32_t width() const { return surface_.width(); }
  uint32_t height() const { return surface_.height(); }
  uint8_t pixelsize() const { return surface_.pixelsize(); }

  virtual void FlushRegion(const virtio_gpu_rect_t& rect);

  void SetResource(GpuResource* res, const virtio_gpu_set_scanout_t* request);

 private:
  GpuBitmap surface_;

  // Scanout parameters.
  GpuResource* resource_ = nullptr;
  GpuRect rect_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_SCANOUT_H_
