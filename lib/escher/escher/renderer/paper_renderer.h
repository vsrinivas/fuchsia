// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/renderer/renderer.h"

namespace escher {

class PaperRenderer : public Renderer {
 public:
  void DrawFrame(Stage& stage,
                 Model& model,
                 const FramebufferPtr& framebuffer,
                 const SemaphorePtr& frame_done,
                 FrameRetiredCallback frame_retired_callback) override;

  // Given a color image, create a framebuffer into which clients can draw
  // frames.
  FramebufferPtr NewFramebuffer(const ImagePtr& image) override;

  // Set whether one or more debug-overlays is to be show.
  void set_show_debug_info(bool b) { show_debug_info_ = b; }

 private:
  friend class Escher;
  PaperRenderer(impl::EscherImpl* escher);
  ~PaperRenderer() override;

  void DrawDepthPrePass(const FramebufferPtr& framebuffer,
                        Stage& stage,
                        Model& model);
  void DrawLightingPass(const FramebufferPtr& framebuffer,
                        Stage& stage,
                        Model& model);

  MeshPtr full_screen_;
  impl::ImageCache* image_cache_;
  vk::Format depth_format_;
  std::unique_ptr<impl::ModelData> model_data_;
  std::unique_ptr<impl::ModelRenderer> model_renderer_;
  std::vector<vk::ClearValue> clear_values_;
  bool show_debug_info_ = false;

  FRIEND_REF_COUNTED_THREAD_SAFE(PaperRenderer);
  FTL_DISALLOW_COPY_AND_ASSIGN(PaperRenderer);
};

}  // namespace escher
