// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scenic/skia/host_canvas_cycler.h"

#include "lib/ftl/logging.h"

namespace scenic_lib {
namespace skia {

HostCanvasCycler::HostCanvasCycler(scenic_lib::Session* session)
    : EntityNode(session),
      content_node_(session),
      content_material_(session),
      surface_pool_(session, kNumBuffers) {
  content_node_.SetMaterial(content_material_);
  AddChild(content_node_);
}

HostCanvasCycler::~HostCanvasCycler() = default;

SkCanvas* HostCanvasCycler::AcquireCanvas(float logical_width,
                                          float logical_height,
                                          float scale_x,
                                          float scale_y) {
  FTL_DCHECK(!acquired_surface_);

  // Update the surface pool and content shape.
  scenic::ImageInfo image_info;
  image_info.width = logical_width * scale_x;
  image_info.height = logical_height * scale_y;
  image_info.stride = image_info.width * 4u;
  image_info.pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
  image_info.color_space = scenic::ImageInfo::ColorSpace::SRGB;
  image_info.tiling = scenic::ImageInfo::Tiling::LINEAR;
  reconfigured_ = surface_pool_.Configure(&image_info);

  // Acquire the surface.
  acquired_surface_ = surface_pool_.GetSkSurface(surface_index_);
  FTL_DCHECK(acquired_surface_);
  logical_width_ = logical_width;
  logical_height_ = logical_height;

  SkCanvas* canvas = acquired_surface_->getCanvas();
  canvas->save();
  canvas->scale(scale_x, scale_y);
  return canvas;
}

void HostCanvasCycler::ReleaseAndSwapCanvas() {
  FTL_DCHECK(acquired_surface_);

  acquired_surface_->getCanvas()->restoreToCount(1);
  acquired_surface_->flush();
  acquired_surface_.reset();

  const scenic_lib::HostImage* image = surface_pool_.GetImage(surface_index_);
  FTL_DCHECK(image);
  content_material_.SetTexture(*image);

  if (reconfigured_) {
    scenic_lib::Rectangle content_rect(content_node_.session(), logical_width_,
                                       logical_height_);
    content_node_.SetShape(content_rect);
    reconfigured_ = false;
  }

  // TODO(MZ-145): Define an |InvalidateOp| on |Image| instead.
  surface_pool_.DiscardImage(surface_index_);
  surface_index_ = (surface_index_ + 1) % kNumBuffers;
}

}  // namespace skia
}  // namespace scenic_lib
