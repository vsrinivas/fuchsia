// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_uniform_writer.h"
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
                vk::Format color_format,
                vk::Format depth_format);
  ~ModelRenderer();
  void Draw(Stage& stage,
            Model& model,
            CommandBuffer* command_buffer,
            const TexturePtr& illumination_texture);

  // TODO: remove
  bool hack_use_depth_prepass = false;

  vk::RenderPass depth_prepass() const { return depth_prepass_; }
  vk::RenderPass lighting_pass() const { return lighting_pass_; }

 private:
  const MeshPtr& GetMeshForShape(const Shape& shape) const;
  void CreateRenderPasses(vk::Format color_format, vk::Format depth_format);

  vk::Device device_;
  vk::RenderPass depth_prepass_;
  vk::RenderPass lighting_pass_;

  MeshManager* mesh_manager_;
  ModelData* model_data_;

  std::unique_ptr<impl::ModelPipelineCache> pipeline_cache_;

  // Avoid per-frame heap allocations.
  std::vector<ModelUniformWriter::PerObjectBinding> per_object_bindings_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  static TexturePtr CreateWhiteTexture(EscherImpl* escher);

  MeshPtr rectangle_;
  MeshPtr circle_;

  TexturePtr white_texture_;
};

}  // namespace impl
}  // namespace escher
