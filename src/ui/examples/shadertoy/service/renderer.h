// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SHADERTOY_SERVICE_RENDERER_H_
#define SRC_UI_EXAMPLES_SHADERTOY_SERVICE_RENDERER_H_

#include "src/ui/examples/shadertoy/service/pipeline.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/impl/descriptor_set_pool.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace shadertoy {

class Renderer;
using RendererPtr = fxl::RefPtr<Renderer>;

class Renderer {
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

  const escher::VulkanContext& vulkan_context() { return context_; }

  escher::Escher* escher() const { return escher_.get(); }
  escher::EscherWeakPtr GetEscherWeakPtr() { return escher_; }

  void DrawFrame(const escher::FramebufferPtr& framebuffer, const PipelinePtr& pipeline,
                 const Params& params, escher::Texture* channel0, escher::Texture* channel1,
                 escher::Texture* channel2, escher::Texture* channel3,
                 escher::SemaphorePtr framebuffer_ready, escher::SemaphorePtr frame_done);

  vk::Format framebuffer_format() const { return framebuffer_format_; }
  vk::RenderPass render_pass() const { return render_pass_; }
  vk::DescriptorSetLayout descriptor_set_layout() const { return descriptor_set_pool_.layout(); }

 private:
  // Update descriptor set with current channel textures.
  vk::DescriptorSet GetUpdatedDescriptorSet(const escher::FramePtr& frame,
                                            escher::Texture* channel0, escher::Texture* channel1,
                                            escher::Texture* channel2, escher::Texture* channel3);

  // Obtain a texture to bind to a descriptor set, either |texture_or_null| or
  // (if it is null) the default white texture.
  escher::Texture* GetChannelTexture(const escher::FramePtr& frame,
                                     escher::Texture* texture_or_null);

  escher::TexturePtr CreateWhiteTexture(escher::BatchGpuUploader* gpu_uploader);

  const escher::VulkanContext context_;
  const escher::EscherWeakPtr escher_;
  std::vector<escher::TexturePtr> depth_buffers_;
  std::vector<escher::TexturePtr> msaa_buffers_;

  vk::Device device_;
  vk::Format framebuffer_format_;
  vk::RenderPass render_pass_;

  escher::MeshPtr full_screen_;
  escher::TexturePtr white_texture_;
  escher::impl::DescriptorSetPool descriptor_set_pool_;

  uint64_t frame_number_ = 0;
};

}  // namespace shadertoy

#endif  // SRC_UI_EXAMPLES_SHADERTOY_SERVICE_RENDERER_H_
