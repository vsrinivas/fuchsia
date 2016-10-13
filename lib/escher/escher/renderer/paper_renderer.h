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

  FramebufferPtr NewFramebuffer(const ImagePtr& image) override;

 private:
  friend class Escher;
  PaperRenderer(impl::EscherImpl* escher);
  ~PaperRenderer() override;

  void BeginModelRenderPass(const FramebufferPtr& framebuffer,
                            vk::CommandBuffer command_buffer);
  impl::ImageCache* image_cache_;
  vk::Format depth_format_;
  vk::RenderPass render_pass_;
  std::unique_ptr<impl::ModelData> model_data_;
  std::unique_ptr<impl::PipelineCache> pipeline_cache_;
  std::unique_ptr<impl::ModelRenderer> model_renderer_;

  FRIEND_REF_COUNTED_THREAD_SAFE(PaperRenderer);
  FTL_DISALLOW_COPY_AND_ASSIGN(PaperRenderer);
};

}  // namespace escher
