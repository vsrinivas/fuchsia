// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_RENDERER_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_RENDERER_H_

#include "lib/escher/renderer/renderer.h"

#include "garnet/examples/ui/shadertoy/service/pipeline.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/impl/descriptor_set_pool.h"
#include "lib/escher/vk/texture.h"

namespace shadertoy {

class Renderer;
using RendererPtr = fxl::RefPtr<Renderer>;

class Renderer : public escher::Renderer {
 public:
  struct Params {
    glm::vec3 iResolution;
    float iTime;
    float iTimeDelta;
    int32_t iFrame;
    float iChannelTime[4];
    glm::vec3 iChannelResolution[4];
    glm::vec4 iMouse;
    glm::vec4 iDate;
    float iSampleRate;

    Params();
  };

  explicit Renderer(escher::EscherWeakPtr escher, vk::Format color_format);

  void DrawFrame(const escher::FramebufferPtr& framebuffer,
                 const PipelinePtr& pipeline, const Params& params,
                 escher::Texture* channel0, escher::Texture* channel1,
                 escher::Texture* channel2, escher::Texture* channel3,
                 escher::SemaphorePtr framebuffer_ready,
                 escher::SemaphorePtr frame_done);

  vk::Format framebuffer_format() const { return framebuffer_format_; }
  vk::RenderPass render_pass() const { return render_pass_; }
  vk::DescriptorSetLayout descriptor_set_layout() const {
    return descriptor_set_pool_.layout();
  }

 private:
  // Update descriptor set with current channel textures.
  vk::DescriptorSet GetUpdatedDescriptorSet(const escher::FramePtr& frame,
                                            escher::Texture* channel0,
                                            escher::Texture* channel1,
                                            escher::Texture* channel2,
                                            escher::Texture* channel3);

  // Obtain a texture to bind to a descriptor set, either |texture_or_null| or
  // (if it is null) the default white texture.
  escher::Texture* GetChannelTexture(const escher::FramePtr& frame,
                                     escher::Texture* texture_or_null);

  escher::TexturePtr CreateWhiteTexture();

  vk::Device device_;
  vk::Format framebuffer_format_;
  vk::RenderPass render_pass_;

  escher::MeshPtr full_screen_;
  escher::TexturePtr white_texture_;
  escher::impl::DescriptorSetPool descriptor_set_pool_;

  uint64_t frame_number_ = 0;
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_RENDERER_H_
