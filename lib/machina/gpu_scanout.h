// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_SCANOUT_H_
#define GARNET_LIB_MACHINA_GPU_SCANOUT_H_

#include <lib/fit/function.h>
#include <virtio/gpu.h>
#include <zircon/types.h>

#include "garnet/lib/machina/gpu_bitmap.h"

namespace machina {

class GpuResource;

// A scanout represents a display that GPU resources can be rendered to.
class GpuScanout {
 public:
  GpuScanout() = default;
  GpuScanout(GpuBitmap surface) : surface_(std::move(surface)) {}

  virtual ~GpuScanout() = default;

  void SetBitmap(GpuBitmap bitmap) { surface_ = std::move(bitmap); }

  uint32_t width() const { return surface_.width(); }
  uint32_t height() const { return surface_.height(); }
  uint32_t stride() const { return surface_.stride(); }
  uint8_t pixelsize() const { return surface_.pixelsize(); }

  // Draws |rect| from the backing resource to the display.
  void DrawScanoutResource(const virtio_gpu_rect_t& rect);

  // Called whenever the scanout bitmap has been redrawn.
  virtual void InvalidateRegion(const GpuRect& rect) {}

  void Draw(const GpuResource& res, const GpuRect& src, const GpuRect& dest);

  virtual void SetResource(GpuResource* res,
                           const virtio_gpu_set_scanout_t* request);

  void MoveOrUpdateCursor(GpuResource* cursor,
                          const virtio_gpu_update_cursor* request);

  using OnReadyCallback = fit::closure;
  void WhenReady(OnReadyCallback callback);

 protected:
  void SetReady(bool ready);

 private:
  void InvokeReadyCallback();
  void DrawCursor();
  void EraseCursor();

  GpuBitmap surface_;

  // Scanout parameters.
  GpuResource* resource_ = nullptr;
  GpuRect rect_;

  GpuResource* cursor_resource_ = nullptr;
  GpuRect cursor_position_;

  bool ready_ = true;
  OnReadyCallback ready_callback_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_SCANOUT_H_
