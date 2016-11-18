// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/framebuffer.h"

#include "escher/impl/escher_impl.h"
#include "escher/renderer/image.h"

namespace escher {

Framebuffer::Framebuffer(vk::Framebuffer framebuffer,
                         impl::EscherImpl* escher,
                         Renderer* renderer,
                         uint32_t width,
                         uint32_t height,
                         std::vector<ImagePtr> images,
                         std::vector<vk::ImageView> image_views,
                         FramebufferPtr extra_framebuffer)
    : impl::Resource(escher),
      framebuffer_(framebuffer),
      renderer_(renderer),
      width_(width),
      height_(height),
      images_(std::move(images)),
      image_views_(std::move(image_views)),
      extra_framebuffer_(std::move(extra_framebuffer)) {
  FTL_DCHECK(framebuffer);
  FTL_DCHECK(escher);
}

Framebuffer::~Framebuffer() {
  vk::Device device = escher()->vulkan_context().device;
  for (auto& image_view : image_views_) {
    device.destroyImageView(image_view);
  }
  image_views_.clear();
  images_.clear();
  device.destroyFramebuffer(framebuffer_);
}

}  // namespace escher
