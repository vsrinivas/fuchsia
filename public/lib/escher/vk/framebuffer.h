// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource.h"

namespace escher {

class Framebuffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Framebuffer(Escher* escher,
              uint32_t width,
              uint32_t height,
              std::vector<ImagePtr> images,
              vk::RenderPass render_pass);
  Framebuffer(impl::EscherImpl* escher_impl,
              uint32_t width,
              uint32_t height,
              std::vector<ImagePtr> images,
              vk::RenderPass render_pass);
  Framebuffer(Escher* escher, ImagePtr color_image, vk::RenderPass render_pass);
  Framebuffer(Escher* escher,
              ImagePtr color_image,
              ImagePtr depth_image,
              vk::RenderPass render_pass);

  ~Framebuffer() override;

  // TODO: make private... client shouldn't need access to this.
  vk::Framebuffer get() { return framebuffer_; }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

  const ImagePtr& get_image(uint32_t index) const { return images_.at(index); }

 private:
  vk::Framebuffer framebuffer_;
  std::vector<vk::ImageView> image_views_;

  uint32_t width_;
  uint32_t height_;

  // These images are not used directly; they just ensure that the images are
  // not destroyed before the Framebuffer is.
  std::vector<ImagePtr> images_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Framebuffer);
};

typedef fxl::RefPtr<Framebuffer> FramebufferPtr;

}  // namespace escher
