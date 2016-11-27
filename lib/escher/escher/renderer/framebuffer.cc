// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/framebuffer.h"

#include "escher/impl/escher_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image.h"

namespace escher {

Framebuffer::Framebuffer(impl::EscherImpl* escher,
                         uint32_t width,
                         uint32_t height,
                         std::vector<ImagePtr> images,
                         vk::RenderPass render_pass)
    : impl::Resource(escher),
      width_(width),
      height_(height),
      images_(std::move(images)) {
  FTL_DCHECK(escher);
  vk::Device device = escher->vulkan_context().device;

  // For each image, construct a corresponding view.
  image_views_.reserve(images_.size());
  for (auto& im : images_) {
    FTL_DCHECK(width == im->width());
    FTL_DCHECK(height == im->height());

    vk::ImageViewCreateInfo info;
    info.viewType = vk::ImageViewType::e2D;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.format = im->format();
    info.image = im->get();
    if (im->has_depth() || im->has_stencil()) {
      if (im->has_depth())
        info.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eDepth;
      if (im->has_stencil())
        info.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
    } else {
      info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    }
    auto im_view = ESCHER_CHECKED_VK_RESULT(device.createImageView(info));
    image_views_.push_back(im_view);
  }

  vk::FramebufferCreateInfo info;
  info.renderPass = render_pass;
  info.attachmentCount = static_cast<uint32_t>(image_views_.size());
  info.pAttachments = image_views_.data();
  info.width = width;
  info.height = height;
  info.layers = 1;
  framebuffer_ = ESCHER_CHECKED_VK_RESULT(device.createFramebuffer(info));
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
