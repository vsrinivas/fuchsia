// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/geometry/types.h"
#include "escher/impl/descriptor_set_pool.h"
#include "escher/impl/resource.h"
#include "escher/scene/stage.h"

namespace escher {
namespace impl {

class GlslToSpirvCompiler;
class Pipeline;

// TODO: document.
// TODO: rename to indicate that it both samples and filters.
class SsdoSampler {
 public:
  // Must match the fragment shader in ssdo_sampler.cc
  const static uint32_t kNoiseSize = 5;

  const static vk::Format kColorFormat = vk::Format::eB8G8R8A8Srgb;

  struct SamplerConfig {
    vec4 key_light;
    vec3 viewing_volume;

    // Convenient way to populate SamplerConfig from a Stage.
    SamplerConfig(const Stage& stage);
  };

  struct FilterConfig {
    vec2 stride;
    float scene_depth;
  };

  static const vk::DescriptorSetLayoutCreateInfo&
  GetDescriptorSetLayoutCreateInfo();

  SsdoSampler(vk::Device device,
              MeshPtr full_screen,
              ImagePtr noise_image,
              GlslToSpirvCompiler* compiler);
  ~SsdoSampler();

  void Sample(CommandBuffer* command_buffer,
              const FramebufferPtr& framebuffer,
              const TexturePtr& depth_texture,
              const SamplerConfig* push_constants,
              const std::vector<vk::ClearValue>& clear_values);

  void Filter(CommandBuffer* command_buffer,
              const FramebufferPtr& framebuffer,
              const TexturePtr& unfiltered_illumination,
              const FilterConfig* push_constants,
              const std::vector<vk::ClearValue>& clear_values);

  // TODO: This is exposed so that PaperRenderer can use it to create
  // Framebuffers, but it would be nice to find a way to remove this.
  vk::RenderPass render_pass() { return render_pass_; }

 private:
  vk::Device device_;
  DescriptorSetPool pool_;
  MeshPtr full_screen_;
  TexturePtr noise_texture_;
  vk::RenderPass render_pass_;
  PipelinePtr sampler_pipeline_;
  PipelinePtr filter_pipeline_;
};

}  // namespace impl
}  // namespace escher
