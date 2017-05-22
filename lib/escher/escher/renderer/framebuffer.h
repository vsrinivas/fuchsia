// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/resources/resource.h"

namespace escher {

class FramebufferCore : public ResourceCore {
 public:
  static const ResourceCoreTypeInfo kTypeInfo;

  FramebufferCore(ResourceLifePreserver* life_preserver,
                  uint32_t width,
                  uint32_t height,
                  const std::vector<ImagePtr>& images,
                  vk::RenderPass render_pass);
  ~FramebufferCore() override;

  vk::Framebuffer get() const { return framebuffer_; }

 private:
  vk::Framebuffer framebuffer_;
  std::vector<vk::ImageView> image_views_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FramebufferCore);
};

class Framebuffer : public Resource2 {
 public:
  Framebuffer(impl::EscherImpl* escher,
              uint32_t width,
              uint32_t height,
              std::vector<ImagePtr> images,
              vk::RenderPass render_pass);
  ~Framebuffer() override;

  // TODO: make private... client shouldn't need access to this.
  vk::Framebuffer get() { return framebuffer_; }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

  const ImagePtr& get_image(uint32_t index) const { return images_.at(index); }

  const FramebufferCore* core() const {
    FTL_DCHECK(
        Resource2::core()->type_info().IsKindOf(FramebufferCore::kTypeInfo));
    return static_cast<const FramebufferCore*>(Resource2::core());
  }

 private:
  void KeepDependenciesAlive(impl::CommandBuffer* command_buffer) override;

  vk::Framebuffer framebuffer_;

  uint32_t width_;
  uint32_t height_;

  // These images are not used directly; they just ensure that the images are
  // not destroyed before the Framebuffer is.
  std::vector<ImagePtr> images_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Framebuffer);
};

typedef ftl::RefPtr<Framebuffer> FramebufferPtr;

}  // namespace escher
