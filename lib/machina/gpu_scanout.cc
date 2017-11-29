// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_scanout.h"

#include "garnet/lib/machina/gpu_resource.h"
#include "third_party/skia/include/core/SkCanvas.h"

void GpuScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuResource* res = resource_;
  if (res == nullptr) {
    return;
  }
  SkCanvas* canvas = surface_->getCanvas();
  SkRect surface_rect = SkRect::MakeIWH(surface_->width(), surface_->height());
  canvas->drawBitmapRect(res->bitmap(), rect_, surface_rect, nullptr);
}

zx_status_t GpuScanout::SetResource(GpuResource* res,
                                    const virtio_gpu_set_scanout_t* request) {
  GpuResource* old_res = resource_;
  resource_ = res;
  if (resource_ == nullptr) {
    if (old_res != nullptr)
      old_res->DetachFromScanout();
    return ZX_OK;
  }
  resource_->AttachToScanout(this);
  rect_ = SkRect::MakeXYWH(
      SkIntToScalar(request->r.x), SkIntToScalar(request->r.y),
      SkIntToScalar(request->r.width), SkIntToScalar(request->r.height));
  return ZX_OK;
}
