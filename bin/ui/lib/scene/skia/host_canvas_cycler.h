// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scene/skia/host_surfaces.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace mozart {
namespace skia {

// Creates a node which presents double-buffered content drawn to a Skia canvas
// using software rendering.
class HostCanvasCycler : public mozart::client::EntityNode {
 public:
  HostCanvasCycler(mozart::client::Session* session);
  ~HostCanvasCycler();

  // Acquires a canvas for rendering.
  // At most one canvas can be acquired at a time.
  // The client is responsible for clearing the canvas.
  SkCanvas* AcquireCanvas(uint32_t width, uint32_t height);

  // Releases the canvas most recently acquired using |AcquireCanvas()|.
  // Sets the content node's texture to be backed by the canvas.
  void ReleaseAndSwapCanvas();

 private:
  static constexpr uint32_t kNumBuffers = 2u;

  mozart::client::ShapeNode content_node_;
  mozart::client::Material content_material_;
  mozart::skia::HostSkSurfacePool surface_pool_;
  sk_sp<SkSurface> acquired_surface_;
  bool reconfigured_ = false;
  uint32_t surface_index_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostCanvasCycler);
};

}  // namespace skia
}  // namespace mozart
