// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "framebuffers.h"

#include "src/graphics/examples/vkprimer/common/utils.h"

namespace vkp {

Framebuffers::Framebuffers(std::shared_ptr<vk::Device> device, const vk::Extent2D &extent,
                           vk::RenderPass render_pass, std::vector<vk::ImageView> image_views)
    : initialized_(false),
      device_(device),
      extent_(extent),
      image_views_(std::move(image_views)),
      render_pass_(render_pass) {}

bool Framebuffers::Init() {
  RTN_IF_MSG(false, initialized_, "Framebuffers is already initialized.\n");

  vk::FramebufferCreateInfo framebuffer_info;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.renderPass = render_pass_;
  framebuffer_info.width = extent_.width;
  framebuffer_info.height = extent_.height;
  framebuffer_info.layers = 1;
  for (const auto &image_view : image_views_) {
    framebuffer_info.setPAttachments(&image_view);
    auto [r_framebuffer, framebuffer] = device_->createFramebufferUnique(framebuffer_info);
    RTN_IF_VKH_ERR(false, r_framebuffer, "Failed to create framebuffer.\n");
    framebuffers_.emplace_back(std::move(framebuffer));
  }

  initialized_ = true;
  return true;
}

const std::vector<vk::UniqueFramebuffer> &Framebuffers::framebuffers() const {
  return framebuffers_;
}

}  // namespace vkp
