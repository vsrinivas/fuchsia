// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/host_image_cycler.h"

#include <zircon/assert.h>

namespace scenic {

HostImageCycler::HostImageCycler(Session* session)
    : EntityNode(session),
      content_node_(session),
      content_material_(session),
      image_pool_(session, kNumBuffers) {
  content_node_.SetMaterial(content_material_);
  AddChild(content_node_);
}

HostImageCycler::~HostImageCycler() = default;

const HostImage* HostImageCycler::AcquireImage(
    uint32_t width, uint32_t height, uint32_t stride,
    fuchsia::images::PixelFormat pixel_format,
    fuchsia::images::ColorSpace color_space) {
  ZX_DEBUG_ASSERT(!acquired_image_);

  // Update the image pool and content shape.
  fuchsia::images::ImageInfo image_info;
  image_info.width = width;
  image_info.height = height;
  image_info.stride = stride;
  image_info.pixel_format = pixel_format;
  image_info.color_space = color_space;
  image_info.tiling = fuchsia::images::Tiling::LINEAR;
  reconfigured_ = image_pool_.Configure(&image_info);

  const HostImage* image = image_pool_.GetImage(image_index_);
  ZX_DEBUG_ASSERT(image);
  acquired_image_ = true;
  return image;
}

void HostImageCycler::ReleaseAndSwapImage() {
  ZX_DEBUG_ASSERT(acquired_image_);
  acquired_image_ = false;

  const HostImage* image = image_pool_.GetImage(image_index_);
  ZX_DEBUG_ASSERT(image);
  content_material_.SetTexture(*image);

  if (reconfigured_) {
    Rectangle content_rect(content_node_.session(),
                           image_pool_.image_info()->width,
                           image_pool_.image_info()->height);
    content_node_.SetShape(content_rect);
    reconfigured_ = false;
  }

  // TODO(MZ-145): Define an |InvalidateCmd| on |Image| instead.
  image_pool_.DiscardImage(image_index_);
  image_index_ = (image_index_ + 1) % kNumBuffers;
}

}  // namespace scenic
