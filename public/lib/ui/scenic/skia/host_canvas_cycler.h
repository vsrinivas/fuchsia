// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/scenic/skia/host_surfaces.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace scenic_lib {
namespace skia {

// Creates a node which presents double-buffered content drawn to a Skia canvas
// using software rendering.
class HostCanvasCycler : public EntityNode {
 public:
  HostCanvasCycler(Session* session);
  ~HostCanvasCycler();

  // Acquires a canvas for rendering.
  // At most one canvas can be acquired at a time.
  // The client is responsible for clearing the canvas.
  SkCanvas* AcquireCanvas(float logical_width,
                          float logical_height,
                          float scale_x,
                          float scale_y);

  // Releases the canvas most recently acquired using |AcquireCanvas()|.
  // Sets the content node's texture to be backed by the canvas.
  void ReleaseAndSwapCanvas();

 private:
  static constexpr uint32_t kNumBuffers = 2u;

  ShapeNode content_node_;
  Material content_material_;
  HostSkSurfacePool surface_pool_;
  sk_sp<SkSurface> acquired_surface_;
  bool reconfigured_ = false;
  uint32_t surface_index_ = 0u;
  float logical_width_ = 0.f;
  float logical_height_ = 0.f;

  FXL_DISALLOW_COPY_AND_ASSIGN(HostCanvasCycler);
};

}  // namespace skia
}  // namespace scenic_lib
