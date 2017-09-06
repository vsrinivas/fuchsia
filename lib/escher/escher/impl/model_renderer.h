// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_display_list_flags.h"
#include "escher/renderer/texture.h"
#include "escher/shape/mesh.h"

namespace escher {
namespace impl {

class ModelData;

// ModelRenderer is a subcomponent used by PaperRenderer.
class ModelRenderer {
 public:
  ModelRenderer(EscherImpl* escher,
                ModelData* model_data,
                vk::Format pre_pass_color_format,
                vk::Format lighting_pass_color_format,
                uint32_t lighting_pass_sample_count,
                vk::Format depth_format);
  ~ModelRenderer();
  void Draw(const Stage& stage,
            const ModelDisplayListPtr& display_list,
            CommandBuffer* command_buffer);

  // TODO: remove
  bool hack_use_depth_prepass = false;

  vk::RenderPass depth_prepass() const { return depth_prepass_; }
  vk::RenderPass lighting_pass() const { return lighting_pass_; }

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

 private:
  void CreateRenderPasses(vk::Format pre_pass_color_format,
                          vk::Format lighting_pass_color_format,
                          uint32_t lighting_pass_sample_count,
                          vk::Format depth_format);

  vk::Device device_;
  vk::RenderPass depth_prepass_;
  vk::RenderPass lighting_pass_;

  ResourceRecycler* const resource_recycler_;
  MeshManager* const mesh_manager_;
  ModelData* const model_data_;

  std::unique_ptr<impl::ModelPipelineCache> pipeline_cache_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  static TexturePtr CreateWhiteTexture(EscherImpl* escher);

  MeshPtr rectangle_;
  MeshPtr circle_;

  TexturePtr white_texture_;
};

}  // namespace impl
}  // namespace escher
