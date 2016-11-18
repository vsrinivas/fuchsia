// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/impl/resource.h"

namespace escher {

class Framebuffer : public impl::Resource {
 public:
  ~Framebuffer();

  // TODO: make private... client shouldn't need access to this.
  vk::Framebuffer framebuffer() { return framebuffer_; }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

  const ImagePtr& get_image(uint32_t index) const { return images_.at(index); }

  // TODO: this is a hack for associating two framebuffers, where one uses
  // a subset of the images/views of the other one.  Here's the use-case: if
  // you have a framebuffer with a color and a depth attachment, then you might
  // have a render-pass that just wants to render into the color attachment.
  // Unfortunately, Vulkan doesn't let you use the same framebuffer (or at least
  // the validation layers don't)... you need to create a separate framebuffer.
  const FramebufferPtr& extra_framebuffer() { return extra_framebuffer_; }

 private:
  friend class Renderer;

  // Called by Renderer::CreateFramebuffer().
  Framebuffer(vk::Framebuffer framebuffer,
              impl::EscherImpl* escher,
              Renderer* renderer,
              uint32_t width,
              uint32_t height,
              std::vector<ImagePtr> images,
              std::vector<vk::ImageView> image_views,
              FramebufferPtr extra_framebuffer = nullptr);

  vk::Framebuffer framebuffer_;

  // Used to verify that Framebuffer is only used with the Renderer that created
  // it, since that is the one that created the render-pass.
  Renderer* renderer_;

  uint32_t width_;
  uint32_t height_;

  // These images are not used directly; they just ensure that the images are
  // not destroyed before the Framebuffer is.
  std::vector<ImagePtr> images_;
  std::vector<vk::ImageView> image_views_;

  FramebufferPtr extra_framebuffer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Framebuffer);
};

}  // namespace escher
