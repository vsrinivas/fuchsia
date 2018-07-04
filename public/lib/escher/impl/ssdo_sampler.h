// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_SSDO_SAMPLER_H_
#define LIB_ESCHER_IMPL_SSDO_SAMPLER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/impl/compute_shader.h"
#include "lib/escher/impl/descriptor_set_pool.h"
#include "lib/escher/scene/stage.h"

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

  // Radius of shadows, in screen pixels.
  // Must match the fragment shader in ssdo_sampler.cc
  const static uint32_t kShadowRadius = 16;

  // Amount by which the SsdoAccelerator table is scaled down in each dimension,
  // not including bit-packing.
  // Must match the fragment shader in ssdo_sampler.cc
  constexpr static uint32_t kSsdoAccelDownsampleFactor = 8;

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

  SsdoSampler(EscherWeakPtr escher, MeshPtr full_screen, ImagePtr noise_image,
              ModelData* model_data);
  ~SsdoSampler();

  vk::Format color_format() const { return color_format_; }

  // Stochastic sampling to determine obscurance.  The output requires filtering
  // to reduce noise.
  void Sample(CommandBuffer* command_buffer,
              const escher::FramebufferPtr& framebuffer,
              const TexturePtr& depth_texture,
              const TexturePtr& accelerator_texture,
              const SamplerConfig* push_constants);

  // Filter the noisy output from Sample().  This should be called twice, to
  // filter in a horizontal and a vertical direction (the direction is selected
  // by the FilterConfig's 'stride' parameter).
  void Filter(CommandBuffer* command_buffer,
              const escher::FramebufferPtr& framebuffer,
              const TexturePtr& unfiltered_illumination,
              const TexturePtr& accelerator_texture,
              const FilterConfig* push_constants);

  // TODO: This is exposed so that PaperRenderer can use it to create
  // Framebuffers, but it would be nice to find a way to remove this.
  vk::RenderPass render_pass() { return render_pass_; }

 private:
  const vk::Device device_;
  const vk::Format color_format_;
  DescriptorSetPool pool_;
  MeshPtr full_screen_;
  TexturePtr noise_texture_;
  vk::RenderPass render_pass_;
  PipelinePtr sampler_pipeline_;
  PipelinePtr filter_pipeline_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_SSDO_SAMPLER_H_
