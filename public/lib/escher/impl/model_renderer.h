// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list_flags.h"
#include "lib/escher/impl/model_pipeline_cache.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/texture.h"

namespace escher {
namespace impl {

class ModelData;

// ModelRenderer is a subcomponent used by PaperRenderer.
class ModelRenderer {
 public:
  ModelRenderer(Escher* escher, ModelDataPtr model_data);
  ~ModelRenderer();
  void Draw(const Stage& stage,
            const ModelDisplayListPtr& display_list,
            CommandBuffer* command_buffer);

  // TODO: remove
  bool hack_use_depth_prepass = false;

  vk::RenderPass depth_prepass() const {
    return pipeline_cache_->depth_prepass();
  }
  vk::RenderPass lighting_pass() const {
    return pipeline_cache_->lighting_pass();
  }

  // Returns a single-pixel white texture.  Do with it what you will.
  const TexturePtr& white_texture() const { return white_texture_; }

  impl::ModelPipelineCache* pipeline_cache() const {
    return pipeline_cache_.get();
  }

  ResourceRecycler* resource_recycler() const { return resource_recycler_; }

  ModelDisplayListPtr CreateDisplayList(const Stage& stage,
                                        const Model& model,
                                        const Camera& camera,
                                        ModelDisplayListFlags flags,
                                        float scale,
                                        uint32_t sample_count,
                                        const TexturePtr& illumination_texture,
                                        CommandBuffer* command_buffer);

  const MeshPtr& GetMeshForShape(const Shape& shape) const;

  // Update the pipeline cache to reflect any changes to the desired render
  // passes.
  void UpdatePipelineCache(vk::Format pre_pass_color_format,
                           vk::Format lighting_pass_color_format,
                           uint32_t lighting_pass_sample_count);

 private:
  std::pair<vk::RenderPass, vk::RenderPass> CreateRenderPasses(
      vk::Format pre_pass_color_format,
      vk::Format lighting_pass_color_format,
      uint32_t lighting_pass_sample_count,
      vk::Format depth_format);

  Escher* const escher_;
  vk::Device device_;

  ResourceRecycler* const resource_recycler_;
  ModelDataPtr model_data_;
  ModelPipelineCachePtr pipeline_cache_;

  // Any time that these change, a new pipeline cache must be generated.
  vk::Format pre_pass_color_format_;
  vk::Format lighting_pass_color_format_;
  uint32_t lighting_pass_sample_count_ = 0;
  vk::Format depth_format_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  static TexturePtr CreateWhiteTexture(Escher* escher);

  MeshPtr rectangle_;
  MeshPtr circle_;

  TexturePtr white_texture_;
};

}  // namespace impl
}  // namespace escher
