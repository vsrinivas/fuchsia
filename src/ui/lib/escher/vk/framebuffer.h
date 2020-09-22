// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_FRAMEBUFFER_H_
#define SRC_UI_LIB_ESCHER_VK_FRAMEBUFFER_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"

namespace escher {

// TODO(fxbug.dev/7170): RenderPass and Framebuffer are deprecated, to be replaced by
// impl::RenderPass and impl::Framebuffer.  The latter two aren't visible to
// Escher clients; they are an implementation detail of escher::CommandBuffer
// (note: NOT escher::impl::CommandBuffer, which is also deprecated).
class Framebuffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Framebuffer(Escher* escher, uint32_t width, uint32_t height, std::vector<ImagePtr> images,
              vk::RenderPass render_pass);
  Framebuffer(Escher* escher, ImagePtr color_image, vk::RenderPass render_pass);
  Framebuffer(Escher* escher, ImagePtr color_image, ImagePtr depth_image,
              vk::RenderPass render_pass);

  ~Framebuffer() override;

  // TODO: make private... client shouldn't need access to this.
  vk::Framebuffer vk() { return framebuffer_; }

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

#endif  // SRC_UI_LIB_ESCHER_VK_FRAMEBUFFER_H_
