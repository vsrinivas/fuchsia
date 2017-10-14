// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/framebuffer.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/escher_impl.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/renderer/image.h"
#include "lib/escher/resources/resource_recycler.h"

namespace escher {

const ResourceTypeInfo Framebuffer::kTypeInfo("Framebuffer",
                                              ResourceType::kResource,
                                              ResourceType::kFramebuffer);

Framebuffer::Framebuffer(impl::EscherImpl* escher_impl,
                         uint32_t width,
                         uint32_t height,
                         std::vector<ImagePtr> images,
                         vk::RenderPass render_pass)
    : Framebuffer(escher_impl->escher(),
                  width,
                  height,
                  std::move(images),
                  render_pass) {}

Framebuffer::Framebuffer(Escher* escher,
                         ImagePtr color_image,
                         vk::RenderPass render_pass)
    : Framebuffer(escher,
                  color_image->width(),
                  color_image->height(),
                  std::vector<ImagePtr>{std::move(color_image)},
                  render_pass) {}

Framebuffer::Framebuffer(Escher* escher,
                         ImagePtr color_image,
                         ImagePtr depth_image,
                         vk::RenderPass render_pass)
    : Framebuffer(
          escher,
          color_image->width(),
          color_image->height(),
          std::vector<ImagePtr>{std::move(color_image), std::move(depth_image)},
          render_pass) {}

Framebuffer::Framebuffer(Escher* escher,
                         uint32_t width,
                         uint32_t height,
                         std::vector<ImagePtr> images,
                         vk::RenderPass render_pass)
    : Resource(escher->resource_recycler()),
      width_(width),
      height_(height),
      images_(std::move(images)) {
  vk::Device device = vulkan_context().device;

  // For each image, construct a corresponding view.
  image_views_.reserve(images.size());
  for (auto& im : images_) {
    FXL_DCHECK(width == im->width());
    FXL_DCHECK(height == im->height());

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
  vk::Device device = vulkan_context().device;
  for (auto& image_view : image_views_) {
    device.destroyImageView(image_view);
  }
  device.destroyFramebuffer(framebuffer_);
}

}  // namespace escher
